# ------------------------------------------------------------
# 要求使用的 CMake 最低版本
# 作用：防止用过老的 CMake，导致命令/行为不一致
# ------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

# ------------------------------------------------------------
# 定义工程名称和使用的语言
# opencv_ffmpeg_demo ：工程名
# LANGUAGES CXX       ：只启用 C++ 编译链路
# ------------------------------------------------------------
project(opencv_ffmpeg_demo LANGUAGES CXX)

# ------------------------------------------------------------
# 设置 C++ 标准为 C++17
# 相当于给编译器加上：-std=c++17
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 11)

# 强制要求使用 C++17，不允许自动降级
# 否则有些编译器可能偷偷用 C++14 / C++11
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 1) 查找 OpenCV 库
# ============================================================

# 在系统中查找 OpenCV
# 会读取 OpenCVConfig.cmake（由 OpenCV 安装时提供）
# REQUIRED：如果没找到，直接报错并停止配置
find_package(OpenCV REQUIRED)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libs: ${OpenCV_LIBS}")
# 找到 OpenCV 后，通常会提供：
#   OpenCV_INCLUDE_DIRS  -> 头文件路径（编译期使用）
#   OpenCV_LIBS          -> 需要链接的库（链接期使用）
#   OpenCV_VERSION       -> OpenCV 版本号


# ============================================================
# 2) 查找 FFmpeg（通过 pkg-config）
# ============================================================

# 查找 pkg-config 工具
# pkg-config 的作用：根据 .pc 文件自动给出
#   -I (include 路径)
#   -L (library 路径)
#   -l (要链接的库)
find_package(PkgConfig REQUIRED)

# 使用 pkg-config 查找 FFmpeg 相关模块
#
# FFMPEG            ：模块前缀名（可自定义）
# REQUIRED          ：缺任何一个模块就报错
# IMPORTED_TARGET   ：生成一个可直接链接的 CMake 目标
#
# 这些模块会对应系统中的：
#   libavcodec.pc
#   libavformat.pc
#   libavutil.pc
#   libswscale.pc
#   libswresample.pc
#
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
    libavdevice
)

# 上面这条命令会生成一个目标：
#   PkgConfig::FFMPEG
#
# 这个 target 内部已经包含：
#   - FFmpeg 的头文件路径（-I）
#   - FFmpeg 的库路径（-L）
#   - FFmpeg 的链接库（-lavcodec -lavformat ...）
#
# 后面只需要链接这个 target 即可


# ============================================================
# 3) 定义可执行程序
# ============================================================

# 定义一个可执行文件：
#   目标名：test
#   源文件：test.cpp
add_executable(video2yuv video2yuv.cpp)


# ============================================================
# 4) 链接库（最关键的一步）
# ============================================================

# 为 test 这个可执行程序链接依赖库
#
# PRIVATE 表示：
#   这些库只对 test 生效
#   不会传递给依赖 test 的其他目标（当前项目只有一个 target）
#
target_link_libraries(video2yuv PRIVATE
    ${OpenCV_LIBS}     # OpenCV 所需的所有库
    PkgConfig::FFMPEG # FFmpeg（通过 pkg-config 自动整理好的目标）
)

# 这一句等价于你手写命令里的：
#   -lopencv_core -lopencv_imgproc ...
#   -lavcodec -lavformat -lavutil -lswscale -lswresample


# ============================================================
# 5) 头文件路径（编译期）
# ============================================================

# 为 test 添加 OpenCV 的头文件搜索路径
# 相当于编译器参数中的：-I/path/to/opencv/include
target_include_directories(video2yuv PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

# 注意：
# FFmpeg 的头文件路径不需要手动写
# 因为 PkgConfig::FFMPEG 已经自动包含了
