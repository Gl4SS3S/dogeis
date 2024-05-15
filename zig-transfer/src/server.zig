const std = @import("std");
const os = std.os.linux;

pub fn main() !void {
    // NOTE: Target the linux OS since it shared the same unix APIs - could break in future
    const fd = os.socket(os.AF.INET, os.SOCK.STREAM, 0);

    if (fd < 0) {
        std.debug.print("setting up socket failed", .{});
    }

    // var val: i32 = 1;
    os.setsockopt(@intCast(fd), os.SOL.SOCKET, os.SO.REUSEADDR, &std.mem.toBytes(@as(c_int, 1)));
}
