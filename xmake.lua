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
    -- GLOW_FADE_DEBUG=1 (PC Runner\pluginbuild.py, debug=True) builds the TESTING DLL with the fading module's Debug page:
    -- it ships only in the PDB and Source download (his call 2026-10-09), never in the main one
    if os.getenv("GLOW_FADE_DEBUG") == "1" then
        add_defines("FADE_DEBUG_PAGE")
    end
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
    -- every warning in OUR files is an error (his C++ standard, 2026-10-07: "/W4 /WX ... fix all warnings"); the
    -- headers included with <...> (CommonLib, the standard library) are external and quiet
    set_warnings("allextra", "error")
    add_cxflags("/external:anglebrackets", "/external:W0")
end)
