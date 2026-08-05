# Enable Qt-specific automatic code generation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 1. Find Qt6 Widgets
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 2. Find PulseAudio / PipeWire client API via PkgConfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

# 3. Create a unified list of dependencies for easy linking
set(APP_DEPENDENCIES
    Qt6::Widgets
    PkgConfig::PULSE
)
