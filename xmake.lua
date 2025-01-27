set_project("Player")
set_xmakever("2.9.7")

set_languages("c++20")

add_rules("mode.debug", "mode.release")

set_warnings("allextra")

add_requires("ffmpeg")
add_requires("libsdl")
add_requires("fmt")

target("player")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("include")
    add_packages("libsdl", "ffmpeg", "fmt")
    -- on_run(function (target)
    --     import("core.base.option")
    --     local argv = {}
    --     table.insert(argv, "--leak-check=full")
    --     table.insert(argv, target:targetfile())
    --     table.insert(argv, option.get("arguments")[1])
    --     os.execv("valgrind", argv)
    -- end)
