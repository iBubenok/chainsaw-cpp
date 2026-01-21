// ==============================================================================
// srum.cpp - Реализация SRUM Analyser
// ==============================================================================
//
// MOD-0011 analyse::srum
// SLICE-017: Analyse SRUM Command Implementation
// SPEC-SLICE-017: micro-spec поведения
//
// ==============================================================================

#include <algorithm>
#include <chainsaw/esedb.hpp>
#include <chainsaw/hve.hpp>
#include <chainsaw/platform.hpp>
#include <chainsaw/srum.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>

namespace chainsaw::analyse {

// ============================================================================
// bytes_to_sid_string - конвертация SID байтов в строку
// ============================================================================
//
// SPEC-SLICE-017 FACT-003..007: bytes_to_sid_string()
// Соответствие Rust srum.rs:40-64
//

std::optional<std::string> bytes_to_sid_string(const std::vector<std::uint8_t>& bytes) {
    // SPEC-SLICE-017 FACT-004: проверка минимального размера (> 8 байт)
    if (bytes.empty() || bytes.size() <= 8) {
        return std::nullopt;
    }

    // SPEC-SLICE-017 FACT-005: SID revision (первый байт)
    std::uint8_t sid_version = bytes[0];

    // SPEC-SLICE-017 FACT-006: authority ID (байты 4-7, big-endian order)
    // Rust: i32::from_le_bytes([hex[7], hex[6], hex[5], hex[4]])
    std::int32_t auth_id = static_cast<std::int32_t>((static_cast<std::uint32_t>(bytes[7]) << 24) |
                                                     (static_cast<std::uint32_t>(bytes[6]) << 16) |
                                                     (static_cast<std::uint32_t>(bytes[5]) << 8) |
                                                     static_cast<std::uint32_t>(bytes[4]));

    // Начинаем строку SID
    std::ostringstream oss;
    oss << "S-" << static_cast<int>(sid_version) << "-" << auth_id;

    // SPEC-SLICE-017 FACT-007: sub-authorities (4-байтовые блоки с байта 8)
    for (std::size_t i = 8; i + 4 <= bytes.size(); i += 4) {
        // Little-endian to int
        std::uint32_t sub_auth = static_cast<std::uint32_t>(bytes[i]) |
                                 (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                 (static_cast<std::uint32_t>(bytes[i + 2]) << 16) |
                                 (static_cast<std::uint32_t>(bytes[i + 3]) << 24);

        oss << "-" << sub_auth;
    }

    return oss.str();
}

// ============================================================================
// format_duration - форматирование длительности
// ============================================================================
//
// SPEC-SLICE-017 FACT-008..013: format_duration()
// Соответствие Rust srum.rs:66-87
//

std::string format_duration(double days) {
    // SPEC-SLICE-017 FACT-009: разделение на целые дни
    auto whole_days = static_cast<std::uint64_t>(std::trunc(days));

    // SPEC-SLICE-017 FACT-010: вычисление часов
    double hours_frac = (days - static_cast<double>(whole_days)) * 24.0;
    auto whole_hours = static_cast<std::uint64_t>(std::round(hours_frac));

    // SPEC-SLICE-017 FACT-011: вычисление минут
    double minutes_frac = (hours_frac - static_cast<double>(whole_hours)) * 60.0;
    auto minutes = static_cast<std::uint64_t>(std::round(minutes_frac));

    // SPEC-SLICE-017 FACT-012..013: сборка строки
    std::vector<std::string> parts;

    if (whole_days > 0) {
        parts.push_back(std::to_string(whole_days) + " days");
    }
    if (whole_hours > 0) {
        parts.push_back(std::to_string(whole_hours) + " hours");
    }
    if (minutes > 0) {
        parts.push_back(std::to_string(minutes) + " minutes");
    }

    // Join with ", "
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            result += ", ";
        result += parts[i];
    }

    return result;
}

// ============================================================================
// win32_ts_to_iso8601 - конвертация Windows timestamp
// ============================================================================

std::string win32_ts_to_iso8601(std::uint64_t win_ts) {
    // Windows timestamp: 100-nanosecond intervals since January 1, 1601
    // Unix epoch: January 1, 1970
    // Difference: 116444736000000000 100-ns intervals
    constexpr std::uint64_t EPOCH_DIFF = 116444736000000000ULL;

    if (win_ts < EPOCH_DIFF) {
        return "";
    }

    auto unix_100ns = win_ts - EPOCH_DIFF;
    auto unix_seconds = unix_100ns / 10000000ULL;

    std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm tm_result{};
#ifdef _WIN32
    if (gmtime_s(&tm_result, &time) != 0) {
        return "";
    }
#else
    if (gmtime_r(&time, &tm_result) == nullptr) {
        return "";
    }
#endif

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << (tm_result.tm_year + 1900) << "-" << std::setw(2)
        << (tm_result.tm_mon + 1) << "-" << std::setw(2) << tm_result.tm_mday << "T" << std::setw(2)
        << tm_result.tm_hour << ":" << std::setw(2) << tm_result.tm_min << ":" << std::setw(2)
        << tm_result.tm_sec << "Z";

    return oss.str();
}

// ============================================================================
// SrumAnalyser implementation
// ============================================================================

SrumAnalyser::SrumAnalyser(std::filesystem::path srum_path,
                           std::filesystem::path software_hive_path)
    : srum_path_(std::move(srum_path)), software_hive_path_(std::move(software_hive_path)) {}

std::optional<SrumDbInfo> SrumAnalyser::parse_srum_database() {
    // SPEC-SLICE-017 FACT-039: Load the SRUM ESE database
    io::esedb::EsedbParser ese_parser;
    if (!ese_parser.load(srum_path_)) {
        const auto& err = ese_parser.last_error();
        error_ = err ? err->format() : "unable to load the ESE database";
        return std::nullopt;
    }

    // Parse all records from ESE database
    auto srum_data = ese_parser.parse();

    // SPEC-SLICE-017 FACT-040: Load the SOFTWARE hive
    io::hve::HveParser hve_parser;
    if (!hve_parser.load(software_hive_path_)) {
        const auto& err = hve_parser.last_error();
        error_ = err ? err->format() : "unable to load the SOFTWARE hive";
        return std::nullopt;
    }

    // SPEC-SLICE-017 FACT-041: Parse SRUM registry entries
    auto srum_reg_info = io::hve::parse_srum_entries(hve_parser);
    if (!srum_reg_info) {
        error_ = "unable to parse the SRUM registry information";
        return std::nullopt;
    }

    // Get parameters and extensions as objects
    auto* srum_params_obj = srum_reg_info->global_parameters.get_object();
    auto* srum_ext_obj = srum_reg_info->extensions.get_object();
    auto* user_info_obj = srum_reg_info->user_info.get_object();

    if (!srum_params_obj || !srum_ext_obj) {
        error_ = "SRUM parameters/extensions should be JSON objects";
        return std::nullopt;
    }

    // Build table details map
    std::map<std::string, TableDetails> table_data_details;

    // SPEC-SLICE-017 FACT-042: Process each extension
    for (auto& [table_guid, extension] : *srum_ext_obj) {
        auto* ext_obj = extension.get_object();
        if (!ext_obj)
            continue;

        // Get table name from "(default)" value
        std::string table_name;
        auto it_default = ext_obj->find("(default)");
        if (it_default != ext_obj->end()) {
            if (auto* str = it_default->second.get_string()) {
                table_name = *str;
            }
        }

        // Get DLL name
        std::optional<std::string> dll_name;
        auto it_dll = ext_obj->find("DllName");
        if (it_dll != ext_obj->end()) {
            if (auto* str = it_dll->second.get_string()) {
                dll_name = *str;
            }
        }

        // Check for long term capability
        auto it_long_term = ext_obj->find("LastLongTermUpdate");
        if (it_long_term != ext_obj->end()) {
            // Get Tier2LongTermPeriod
            double t2_long_term_period = 604800.0;  // default
            auto it_param = srum_params_obj->find("Tier2LongTermPeriod");
            if (it_param != srum_params_obj->end()) {
                if (auto* num = it_param->second.get_double()) {
                    t2_long_term_period = *num;
                } else if (auto* inum = it_param->second.get_int()) {
                    t2_long_term_period = static_cast<double>(*inum);
                }
            }
            // Override from extension if present
            auto it_ext_period = ext_obj->find("Tier2LongTermPeriod");
            if (it_ext_period != ext_obj->end()) {
                if (auto* num = it_ext_period->second.get_double()) {
                    t2_long_term_period = *num;
                } else if (auto* inum = it_ext_period->second.get_int()) {
                    t2_long_term_period = static_cast<double>(*inum);
                }
            }

            // Get Tier2LongTermMaxEntries
            double t2_long_term_max_entries = 260.0;  // default
            it_param = srum_params_obj->find("Tier2LongTermMaxEntries");
            if (it_param != srum_params_obj->end()) {
                if (auto* num = it_param->second.get_double()) {
                    t2_long_term_max_entries = *num;
                } else if (auto* inum = it_param->second.get_int()) {
                    t2_long_term_max_entries = static_cast<double>(*inum);
                }
            }
            auto it_ext_max = ext_obj->find("Tier2LongTermMaxEntries");
            if (it_ext_max != ext_obj->end()) {
                if (auto* num = it_ext_max->second.get_double()) {
                    t2_long_term_max_entries = *num;
                } else if (auto* inum = it_ext_max->second.get_int()) {
                    t2_long_term_max_entries = static_cast<double>(*inum);
                }
            }

            double long_term_retention =
                t2_long_term_period * t2_long_term_max_entries / 3600.0 / 24.0;
            std::string table_guid_lt = table_guid + "LT";
            std::string table_name_lt = table_name + " (Long Term)";

            TableDetails td_lt;
            td_lt.table_name = table_name_lt;
            td_lt.dll_path = dll_name;
            td_lt.retention_time_days = long_term_retention;
            table_data_details[table_guid_lt] = std::move(td_lt);
        }

        // Calculate regular retention time
        double t2_period = 3600.0;  // default
        auto it_t2p = srum_params_obj->find("Tier2Period");
        if (it_t2p != srum_params_obj->end()) {
            if (auto* num = it_t2p->second.get_double()) {
                t2_period = *num;
            } else if (auto* inum = it_t2p->second.get_int()) {
                t2_period = static_cast<double>(*inum);
            }
        }
        auto it_ext_t2p = ext_obj->find("Tier2Period");
        if (it_ext_t2p != ext_obj->end()) {
            if (auto* num = it_ext_t2p->second.get_double()) {
                t2_period = *num;
            } else if (auto* inum = it_ext_t2p->second.get_int()) {
                t2_period = static_cast<double>(*inum);
            }
        }

        double t2_max_entries = 1440.0;  // default
        auto it_t2m = srum_params_obj->find("Tier2MaxEntries");
        if (it_t2m != srum_params_obj->end()) {
            if (auto* num = it_t2m->second.get_double()) {
                t2_max_entries = *num;
            } else if (auto* inum = it_t2m->second.get_int()) {
                t2_max_entries = static_cast<double>(*inum);
            }
        }
        auto it_ext_t2m = ext_obj->find("Tier2MaxEntries");
        if (it_ext_t2m != ext_obj->end()) {
            if (auto* num = it_ext_t2m->second.get_double()) {
                t2_max_entries = *num;
            } else if (auto* inum = it_ext_t2m->second.get_int()) {
                t2_max_entries = static_cast<double>(*inum);
            }
        }

        double retention_time = t2_period * t2_max_entries / 3600.0 / 24.0;

        TableDetails td;
        td.table_name = table_name;
        td.dll_path = dll_name;
        td.retention_time_days = retention_time;
        table_data_details[table_guid] = std::move(td);
    }

    // Parse SruDbIdMapTable for resolving AppId and UserId
    auto sru_id_map = ese_parser.parse_sru_db_id_map_table();

    // Process SRUM data entries
    Value::Array result_array;

    for (auto& srum_entry : srum_data) {
        Value::Object entry_obj;

        // Copy all fields
        for (auto& [key, val] : srum_entry) {
            entry_obj[key] = std::move(val);
        }

        // Resolve AppId -> AppName
        auto it_app_id = entry_obj.find("AppId");
        if (it_app_id != entry_obj.end()) {
            std::string app_id_str;
            if (auto* num = it_app_id->second.get_double()) {
                app_id_str = std::to_string(static_cast<std::int64_t>(*num));
            } else if (auto* inum = it_app_id->second.get_int()) {
                app_id_str = std::to_string(*inum);
            }

            auto it_map = sru_id_map.find(app_id_str);
            if (it_map != sru_id_map.end() && it_map->second.id_blob_as_string) {
                entry_obj["AppName"] = Value(*it_map->second.id_blob_as_string);
            } else {
                entry_obj["AppName"] = Value();  // null
            }
        }

        // Resolve UserId -> UserSID, UserName
        auto it_user_id = entry_obj.find("UserId");
        if (it_user_id != entry_obj.end()) {
            std::string user_id_str;
            if (auto* num = it_user_id->second.get_double()) {
                user_id_str = std::to_string(static_cast<std::int64_t>(*num));
            } else if (auto* inum = it_user_id->second.get_int()) {
                user_id_str = std::to_string(*inum);
            }

            auto it_map = sru_id_map.find(user_id_str);
            if (it_map != sru_id_map.end() && it_map->second.id_blob) {
                auto sid_opt = bytes_to_sid_string(*it_map->second.id_blob);
                if (sid_opt) {
                    entry_obj["UserSID"] = Value(*sid_opt);

                    // Look up username from user_info
                    if (user_info_obj) {
                        auto it_user = user_info_obj->find(*sid_opt);
                        if (it_user != user_info_obj->end()) {
                            if (auto* user_obj = it_user->second.get_object()) {
                                auto it_uname = user_obj->find("Username");
                                if (it_uname != user_obj->end()) {
                                    entry_obj["UserName"] = it_uname->second;
                                }
                            }
                        }
                    }
                }
            }
            if (entry_obj.find("UserName") == entry_obj.end()) {
                entry_obj["UserName"] = Value();  // null
            }
        }

        // Process Table field and resolve TableName
        auto it_table = entry_obj.find("Table");
        if (it_table != entry_obj.end()) {
            if (auto* table_str = it_table->second.get_string()) {
                std::string table_name = *table_str;
                std::string table_name_mut = table_name;

                // If table name starts with '{', it's a GUID
                if (!table_name.empty() && table_name[0] == '{') {
                    if (table_name.size() > 2 && table_name.substr(table_name.size() - 2) == "LT") {
                        // Long term table
                        std::string base_guid = table_name.substr(0, table_name.size() - 2);
                        // Fix closing brace if needed
                        if (!base_guid.empty() && base_guid.back() != '}') {
                            base_guid += "}";
                        }

                        // Uppercase for lookup
                        for (auto& c : base_guid) {
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        }

                        auto it_ext = srum_ext_obj->find(base_guid);
                        if (it_ext != srum_ext_obj->end()) {
                            if (auto* ext_obj2 = it_ext->second.get_object()) {
                                auto it_def = ext_obj2->find("(default)");
                                if (it_def != ext_obj2->end()) {
                                    if (auto* name = it_def->second.get_string()) {
                                        table_name_mut = *name + " (Long Term)";
                                    }
                                }
                            }
                        }
                    } else {
                        // Regular table
                        std::string guid_upper = table_name;
                        for (auto& c : guid_upper) {
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        }

                        auto it_ext = srum_ext_obj->find(guid_upper);
                        if (it_ext != srum_ext_obj->end()) {
                            if (auto* ext_obj2 = it_ext->second.get_object()) {
                                auto it_def = ext_obj2->find("(default)");
                                if (it_def != ext_obj2->end()) {
                                    if (auto* name = it_def->second.get_string()) {
                                        table_name_mut = *name;
                                    }
                                }
                            }
                        }
                    }

                    entry_obj["TableName"] = Value(table_name_mut);
                } else {
                    entry_obj["TableName"] = Value(table_name);
                }

                // Update table timeframe
                auto it_ts = entry_obj.find("TimeStamp");
                if (it_ts != entry_obj.end()) {
                    if (auto* ts_str = it_ts->second.get_string()) {
                        // Update min/max timestamps for this table
                        auto& td = table_data_details[table_name];
                        if (td.table_name.empty()) {
                            td.table_name = table_name_mut;
                        }

                        if (!td.from || *ts_str < *td.from) {
                            td.from = *ts_str;
                        }
                        if (!td.to || *ts_str > *td.to) {
                            td.to = *ts_str;
                        }
                    }
                }
            }
        }

        // Convert Windows timestamps to ISO8601
        const char* win_ts_columns[] = {"EndTime", "ConnectStartTime", "StartTime"};
        for (const char* col_name : win_ts_columns) {
            auto it = entry_obj.find(col_name);
            if (it != entry_obj.end()) {
                std::uint64_t win_ts = 0;
                if (auto* num = it->second.get_double()) {
                    win_ts = static_cast<std::uint64_t>(*num);
                } else if (auto* inum = it->second.get_int()) {
                    win_ts = static_cast<std::uint64_t>(*inum);
                } else if (auto* unum = it->second.get_uint()) {
                    win_ts = *unum;
                }

                if (win_ts > 0) {
                    std::string iso_str = win32_ts_to_iso8601(win_ts);
                    if (!iso_str.empty()) {
                        entry_obj[col_name] = Value(iso_str);
                    }
                }
            }
        }

        result_array.push_back(Value(std::move(entry_obj)));
    }

    // Build result
    SrumDbInfo info;

    // Convert map to vector for table_details
    for (auto& [guid, td] : table_data_details) {
        info.table_details.emplace_back(guid, std::move(td));
    }

    info.db_content = Value(std::move(result_array));

    return info;
}

}  // namespace chainsaw::analyse
