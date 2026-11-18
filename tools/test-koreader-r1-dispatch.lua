-- Host-only KOReader device-dispatch test; DOES NOT touch display/input hardware.
-- The actual upstream frontend/device.lua is executed unmodified except
-- for our one-line R1 environment dispatch addition.
package.preload["ffi/util"] = function()
    return { loadSDL3 = function() error("Unexpected SDL fallback") end }
end
package.preload["version"] = function()
    return { getCurrentPlatform = function() return nil end }
end
local initialized = 0
package.preload["device/hibyr1/device"] = function()
    return {
        model = "HiBy_R1",
        init = function(self)
            assert(self.model == "HiBy_R1")
            initialized = initialized + 1
        end,
    }
end
local arg = {...}
assert(#arg == 1, "Specify the full KOReader device dispatcher")
assert(os.getenv("KO_PLATFORM") == "hibyr1", "R1 KOReader environment is required")
local device = dofile(arg[1])
assert(device.model == "HiBy_R1" and initialized == 1,
       "KOReader did not initialize the HiBy R1 backend exactly once")
print("PASS: real KOReader device dispatcher boots the R1 backend; no SDL fallback")
