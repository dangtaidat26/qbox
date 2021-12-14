-- Virtual platform configuration
-- Commented out parameters show default values

function top()
    local str = debug.getinfo(2, "S").source:sub(2)
    if str:match("(.*/)")
    then
        return str:match("(.*/)")
    else
        return "./"
    end
 end

local conf = {
    [ "platform.kernel_file" ] = top().."fw/linux-demo/Image2",
    [ "platform.dtb_file" ] = top().."fw/linux-demo/virt_512M_simple.dtb",
    [ "platform.bootloader_file" ] = top().."fw/linux-demo/my_debug_hypvm_dump.bin",
    [ "platform.flash_blob_file" ] = top().."fw/linux-demo/rootfs.squashfs",
    [ "platform.with_hexagon" ] = true,
    -- [ "platform.gdb_port" ] = 1234,

    [ "platform.hexagon.sync-policy" ] = "tlm2",
--    [ "platform.hexagon.gdb-port" ] = 1234,
    [ "platform.hexagon_kernel_file" ] = top().."tests/qualcomm/prebuilt/qtimer_test.bin",
    [ "platform.hexagon_load_addr" ] = 0x0,
    [ "platform.hexagon_start_addr" ] = 0x0,
    [ "platform.hexagon_isdb_secure_flag" ] = 0x1,
    [ "platform.hexagon_isdb_trusted_flag" ] = 0x1,
}

for k,v in pairs(conf) do
    _G[k]=v
end
