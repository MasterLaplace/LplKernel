-- The kernel image of the configured mode, for the tasks that pack or boot it (xmake.lua).

import("core.project.config")
import("core.project.project")

-- Builds lpl-kernel and returns its image, build/<plat>/<arch>/<mode>/lpl.kernel. A configuration
-- that compiles the tests refuses an image without them: it would boot without a word, which reads
-- exactly like tests that all passed.
function main()
    os.exec("xmake build lpl-kernel")
    config.load()
    local kernel = project.target("lpl-kernel"):targetfile()
    assert(os.isfile(kernel), format("%s not found: run `xmake` first", kernel))
    if has_config("smoke") and not is_mode("release") then
        local image = io.readfile(kernel, {encoding = "binary"})
        assert(image:find("KTAP version 1", 1, true), format(
            "%s carries no test, though this %s configuration compiles them: rebuild it with `xmake -r lpl-kernel`",
            kernel, config.mode()))
    end
    return kernel
end
