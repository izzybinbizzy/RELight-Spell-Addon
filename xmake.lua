-- RELight - Spell Addon - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("RelightSpellAddon")
set_version("1.6.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- static Visual C++ runtime: the DLL carries its own, so a player with older Visual C++ files cannot crash at load
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", false)

includes("lib/commonlibsse-ng")

-- the fading module's rule files (Fade*.cpp, Illuminated's, ported 2026-10-08)
add_requires("nlohmann_json v3.12.0")

target("RelightSpellAddon", function()
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_defines("NOMINMAX")  -- windows.h min/max macros break std::min/max and numeric_limits::max
    add_rules("commonlibsse-ng.plugin", {
        name = "RelightSpellAddon",
        author = "izzydoingit",
        description = "RELight - Spell Addon - brightness, option switches, travelling lights, lights off while sneaking and fading lights",
    })
    -- the source is split by job (see the file map at the top of src/main.cpp); every .cpp in src is built
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    set_warnings("allextra")
end)
