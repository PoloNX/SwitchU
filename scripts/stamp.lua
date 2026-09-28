-- Build stamps for artefacts generated from a submodule.
--
-- The eSpeak NG data and libstratosphere only depend on their submodule, so
-- they are regenerated when the submodule moves to another commit instead of
-- on every build. A stamp file holds the revision they were produced from.

function revision(dir)
    return try { function ()
        return os.iorunv("git", {"-C", dir, "rev-parse", "HEAD"}):trim()
    end }
end

function path_of(name)
    return path.join(os.projectdir(), "build", ".stamps", name)
end

function matches(name, rev)
    local stamp = path_of(name)
    return rev ~= nil and os.isfile(stamp) and io.readfile(stamp):trim() == rev
end

function write(name, rev)
    if rev then
        io.writefile(path_of(name), rev)
    end
end
