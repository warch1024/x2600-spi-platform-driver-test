#!/bin/sh

# 目标机执行；app_spi 已放在 /tmp
mkdir -p /tmp/spi-stress
cd /tmp

remove_spidev1() {
    test -e /dev/spidev1.0 || return 0

    command -v cmd_spi >/dev/null 2>&1 || {
        echo "遗留 /dev/spidev1.0；缺少 cmd_spi，不能安全注销设备。"
        return 1
    }

    echo "注销遗留 /dev/spidev1.0"
    cmd_spi del_dev /dev/spidev1.0 || return 1
    test ! -e /dev/spidev1.0 || {
        echo "/dev/spidev1.0 注销后仍存在。"
        return 1
    }
}

remove_spidev1 || exit 1

ls -l /dev/fb0 /dev/spidev*
which memtester fb-test-rect cmd_spi

# 极限内存压力：按启动压力前的 MemAvailable 计算，预留 2MiB。
# 该值只控制两个 memtester 的申请量；app_spi 运行后的实际剩余内存会继续波动。
avail_kb=$(awk '$1 == "MemAvailable:" { print $2 }' /proc/meminfo)
reserve_mb=2
each_mb=$(( (avail_kb / 1024 - reserve_mb) / 2 ))

echo "MemAvailable=$((avail_kb / 1024)) MiB, each memtester=${each_mb} MiB"
test "$each_mb" -gt 0 || exit 1

memtester "${each_mb}M" 0 >/tmp/spi-stress/memtester-1.log 2>&1 &
mem1=$!

memtester "${each_mb}M" 0 >/tmp/spi-stress/memtester-2.log 2>&1 &
mem2=$!

fb-test-rect -f 0 -s 1 >/tmp/spi-stress/fb-test-rect.log 2>&1 &
fb=$!
yes >/dev/null 2>&1 &
cpu=$!
sleep 1
kill -0 "$cpu" || exit 1

cleanup() {
    kill "$mem1" "$mem2" "$fb" "$cpu" 2>/dev/null
    wait "$mem1" "$mem2" "$fb" "$cpu" 2>/dev/null
    remove_spidev1
}

trap cleanup EXIT INT TERM

sleep 2
kill -0 "$mem1" || exit 1
kill -0 "$mem2" || exit 1
kill -0 "$fb" || exit 1

echo "MemAvailable after stress start: $(awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo)"

./app_spi --mode always-speed 60000000 --bus 1 --cs pc30 --ssi-source-hz 120000000 --report /tmp/spi-stress/spi1_wave_memfb_cpu.md
result=$?
dmesg > /tmp/spi-stress/dmesg.log

cleanup
trap - EXIT INT TERM
echo "app_spi exit code: $result"


fb-test-rect -f 0 -s 1 >/tmp/spi-stress/fb-test-rect.log 2>&1 &
fb=$!
sleep 2
kill -0 "$fb"

yes >/dev/null 2>&1 &
cpu=$!
sleep 1
kill -0 "$cpu"

memtester 85M 0 >/tmp/spi-stress/memtester-1.log 2>&1 &
mem1=$!
sleep 2
kill -0 "$mem1"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

memtester 25M 0 >/tmp/spi-stress/memtester-2.log 2>&1 &
mem2=$!
sleep 2
kill -0 "$mem2"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

memtester 17M 0 >/tmp/spi-stress/memtester-4.log 2>&1 &
mem4=$!
sleep 2
kill -0 "$mem4"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

memtester 1M 0 >/tmp/spi-stress/memtester-5.log 2>&1 &
mem5=$!
sleep 2
kill -0 "$mem5"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

memtester 1M 0 >/tmp/spi-stress/memtester-6.log 2>&1 &
mem6=$!
sleep 2
kill -0 "$mem6"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

memtester 8192B 0 >/tmp/spi-stress/memtester-7.log 2>&1 &
mem7=$!
sleep 2
kill -0 "$mem7"
awk '$1 == "MemAvailable:" { print $2 / 1024 " MiB" }' /proc/meminfo

kill "$mem1" "$mem2" "$mem3" "$mem4" "$mem5" "$mem6" "$mem7" "$fb" "$cpu" 2>/dev/null
wait "$mem1" "$mem2" "$mem3" "$mem4" "$mem5" "$mem6" "$mem7" "$fb" "$cpu" 2>/dev/null
