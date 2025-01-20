set_project("Player")
set_xmakever("2.9.7")

set_languages("c++20")
set_defaultmode("debug")

set_license("GPL-3.0")

set_encodings("source:utf-8")

add_rules("mode.debug", "mode.release")

add_requires("libsdl", { configs = { sdlmain = false } })
add_requires("ffmpeg")


target("player")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("include")
    add_packages("libsdl", "ffmpeg")
