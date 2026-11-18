-- KOReader device port for the HiBy R1 LCD player (MIPS32 little-endian).
-- This is a port scaffold: compiled KOReader runtime/hardware boot unverified.
-- Keep the upstream KOReader/plugin tree intact for plugin compatibility.
local Generic = require("device/generic/device")
local Framebuffer = require("ffi/framebuffer_linux")
local Input = require("device/input")

local function yes() return true end
local function no() return false end

local R1 = Generic:extend{
    model = "HiBy_R1",
    home_dir = "/mnt/sd_0/Books",
    -- R1 has a small RGB LCD, NOT an E-Ink panel.
    hasEinkScreen = no,
    hasColorScreen = yes,
    isTouchDevice = yes,
    hasMultitouch = no,
    hasKeyboard = no,
    hasKeys = no, -- Hardware key mapping pending verified kernel event trace.
    hasExternalSD = yes,
    hasFrontlight = no,
    hasWifiToggle = no, -- Avoid unverified Wi-Fi command execution.
    hasSeamlessWifiToggle = no,
    hasOTAUpdates = no, -- Firmware updates remain under PocketOS' verified updater.
    canSuspend = no, -- No guessed power-management writes.
    canReboot = no,
    canPowerOff = no,
    canToggleMassStorage = no,
    hasGSensor = no,
    hasColorScreen = yes,
    display_dpi = 160, -- Temporary value until measured on physical R1.
}

function R1:init()
    local touch = os.getenv("KO_R1_TOUCH_DEV")
    if not touch or not touch:match("^/dev/input/event%d+$") then
        error("KOReader R1 port: hardware-probed touch input not set")
    end
    -- Generic Linux FB already handles RGB565 and mmapped LCD pixel buffers;
    -- no E-Ink waveform or Kobo-specific ioctl should ever run on R1.
    self.screen = Framebuffer:new{ device = self }
    self.input = Input:new{ device = self }
    self.input:open(touch)
    self.powerd = require("device/generic/powerd"):new{ device = self }
    Generic.init(self)
end

return R1
