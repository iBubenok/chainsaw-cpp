# ==============================================================================
# Sanitizers.cmake - Конфигурация санитайзеров для C++
# ==============================================================================
#
# REQ-SEC-0022: отсутствие UB и проверяемая безопасность памяти
# GUIDE-0001 G-022: валидация размеров до выделений
# : интеграция базовых quality gates
#
# Использование:
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=address
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=undefined
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=thread
#   cmake -S cpp -B build -DCHAINSAW_SANITIZER=memory  (только clang)
#
# Примечания:
# - ASan и TSan несовместимы друг с другом
# - MSan доступен только в Clang
# - На Windows поддержка санитайзеров ограничена
#
# ==============================================================================

# Опция выбора санитайзера
set(CHAINSAW_SANITIZER "" CACHE STRING "Санитайзер для сборки (address|undefined|thread|memory|none)")

# Проверка платформы и компилятора
if(CHAINSAW_SANITIZER)
    # Нормализация значения
    string(TOLOWER "${CHAINSAW_SANITIZER}" SANITIZER_LOWER)

    # Флаги санитайзеров
    set(SANITIZER_COMPILE_FLAGS "")
    set(SANITIZER_LINK_FLAGS "")

    if(SANITIZER_LOWER STREQUAL "address" OR SANITIZER_LOWER STREQUAL "asan")
        # Address Sanitizer (ASan)
        # Обнаруживает: buffer overflow, use-after-free, double-free, memory leaks
        if(MSVC)
            # MSVC поддерживает ASan начиная с VS 2019 16.9
            list(APPEND SANITIZER_COMPILE_FLAGS "/fsanitize=address")
            message(STATUS "Sanitizer: Address (MSVC mode)")
        else()
            list(APPEND SANITIZER_COMPILE_FLAGS "-fsanitize=address" "-fno-omit-frame-pointer")
            list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=address")
            message(STATUS "Sanitizer: Address (GCC/Clang mode)")
        endif()

    elseif(SANITIZER_LOWER STREQUAL "undefined" OR SANITIZER_LOWER STREQUAL "ubsan")
        # Undefined Behavior Sanitizer (UBSan)
        # Обнаруживает: signed overflow, null pointer dereference, unaligned access, etc.
        if(MSVC)
            message(WARNING "UBSan не поддерживается в MSVC, пропускаю")
        else()
            list(APPEND SANITIZER_COMPILE_FLAGS "-fsanitize=undefined" "-fno-omit-frame-pointer")
            list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=undefined")
            message(STATUS "Sanitizer: Undefined Behavior")
        endif()

    elseif(SANITIZER_LOWER STREQUAL "thread" OR SANITIZER_LOWER STREQUAL "tsan")
        # Thread Sanitizer (TSan)
        # Обнаруживает: data races
        if(MSVC)
            message(WARNING "TSan не поддерживается в MSVC, пропускаю")
        elseif(WIN32)
            message(WARNING "TSan не поддерживается на Windows, пропускаю")
        else()
            list(APPEND SANITIZER_COMPILE_FLAGS "-fsanitize=thread" "-fno-omit-frame-pointer")
            list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=thread")
            message(STATUS "Sanitizer: Thread")
        endif()

    elseif(SANITIZER_LOWER STREQUAL "memory" OR SANITIZER_LOWER STREQUAL "msan")
        # Memory Sanitizer (MSan)
        # Обнаруживает: use of uninitialized memory
        # Доступен только в Clang
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            if(WIN32)
                message(WARNING "MSan не поддерживается на Windows, пропускаю")
            else()
                list(APPEND SANITIZER_COMPILE_FLAGS "-fsanitize=memory" "-fno-omit-frame-pointer" "-fsanitize-memory-track-origins")
                list(APPEND SANITIZER_LINK_FLAGS "-fsanitize=memory")
                message(STATUS "Sanitizer: Memory")
            endif()
        else()
            message(WARNING "MSan доступен только в Clang, пропускаю")
        endif()

    elseif(SANITIZER_LOWER STREQUAL "none" OR SANITIZER_LOWER STREQUAL "")
        message(STATUS "Sanitizer: отключен")

    else()
        message(FATAL_ERROR "Неизвестный санитайзер: ${CHAINSAW_SANITIZER}. Допустимые значения: address, undefined, thread, memory, none")
    endif()

    # Применяем флаги
    if(SANITIZER_COMPILE_FLAGS)
        add_compile_options(${SANITIZER_COMPILE_FLAGS})
    endif()
    if(SANITIZER_LINK_FLAGS)
        add_link_options(${SANITIZER_LINK_FLAGS})
    endif()
endif()

# Функция для добавления санитайзеров к конкретному таргету
# (альтернатива глобальным флагам)
function(target_enable_sanitizers target)
    if(NOT CHAINSAW_SANITIZER OR SANITIZER_LOWER STREQUAL "none")
        return()
    endif()

    if(SANITIZER_COMPILE_FLAGS)
        target_compile_options(${target} PRIVATE ${SANITIZER_COMPILE_FLAGS})
    endif()
    if(SANITIZER_LINK_FLAGS)
        target_link_options(${target} PRIVATE ${SANITIZER_LINK_FLAGS})
    endif()
endfunction()
