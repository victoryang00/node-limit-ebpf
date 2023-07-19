#!/bin/bash
# Modfied from SMDK https://github.com/OpenMPDK/SMDK
readonly BASEDIR=$(readlink -f $(dirname $0))/
green=$(tput bold)$(tput setaf 2)
red=$(tput bold)$(tput setaf 1)
rst=$(tput sgr0)

function check_attr_set() { # attribute, like "e"
  case "$-" in
    *"$1"*) return 1 ;;
    *)    return 0 ;;
  esac
}

function print_pass_fail(){
    #$* > /dev/null 2>&1 # quite mode
    $* 2>&1                             # verbose
    ret=$?
    if (( $ret )); then
        echo ${red}"FAILED!($ret)"${rst}
        echo "Failed running command: "
        echo  "   $*"
        #exit 1  # exit when a test fails
    else
        echo ${green}"PASSED!"${rst}
    fi
        return $ret
}

function run_test(){
    LINE="$*"
    printf  "  %-3s   %-70s : " ${green}"RUN"${rst} "${LINE::69}"
    print_pass_fail $*
        return $?
}

function set_red(){
        echo -e "\033[31m"
}
function set_green(){
        echo -e "\033[32m"
}

function set_white(){
        echo -e "\033[0m"
}

function log_normal(){
        set_green && echo $1 && set_white
}

function log_error(){
        set_red && echo $1 && set_white
}

function killprocess() {
    # $1 = process pid
    if [ -z "$1" ]; then
        exit 1
    fi

    if kill -0 $1; then
        if [ $(uname) = Linux ]; then
            process_name=$(ps --no-headers -o comm= $1)
        else
            process_name=$(ps -c -o command $1 | tail -1)
        fi
        if [ "$process_name" = "sudo" ]; then
            # kill the child process, which is the actual app
            # (assume $1 has just one child)
            local child
            child="$(pgrep -P $1)"
            echo "killing process with pid $child"
            kill $child
        else
            echo "killing process with pid $1"
            kill $1
        fi

        # wait for the process regardless if its the dummy sudo one
        # or the actual app - it should terminate anyway
        wait $1
    else
        # the process is not there anymore
        echo "Process with pid $1 is not found"
        exit 1
    fi
}

SMDK_KERNEL_PATH=${BASEDIR}/Bede-linux/
ROOTFS_PATH=${BASEDIR}
MONITOR_PORT=45454

QEMU_SYSTEM_BINARY=`which qemu-system-x86_64`
BZIMAGE_PATH=${SMDK_KERNEL_PATH}/arch/x86_64/boot/bzImage
INITRD_PATH=/boot/initrd.img-6.4.0+
IMAGE_PATH=${ROOTFS_PATH}/qemu-image.img

function print_usage(){
	echo ""
	echo "Usage:"
	echo " $0 [-x vm_index(0-9)]"
	echo ""
}

while getopts "x:" opt; do
	case "$opt" in
		x)
			if [ $OPTARG -lt 0 ] || [ $OPTARG -gt 9 ]; then
				echo "Error: VM count should be 0-9"
				exit 2
			fi
			VMIDX=$OPTARG
			;;
		*)
			print_usage
			exit 2
			;;
	esac
done

if [ -z ${VMIDX} ]; then
	NET_OPTION="-net user,hostfwd=tcp::2242-:22,hostfwd=tcp::6379-:6379,hostfwd=tcp::11211-:11211, -net nic"
else
	echo "Info: Running VM #${VMIDX}..."
	MONITOR_PORT="4545${VMIDX}"
	IMAGE_PATH=$(echo ${IMAGE_PATH} | sed 's/.img/-'"${VMIDX}"'.img/')
	MACADDR="52:54:00:12:34:${VMIDX}${VMIDX}"
	TAPNAME="tap${VMIDX}"
	NET_OPTION="-net nic,macaddr=${MACADDR} -net tap,ifname=${TAPNAME},script=no"

	IFCONFIG_TAPINFO=`ifconfig | grep ${TAPNAME}`
	if [ -z "${IFCONFIG_TAPINFO}" ]; then
		log_error "${TAPNAME} SHOULD be up for using network in VM. Run 'setup_bridge.sh' in /path/to/lib/qemu/"
		exit 2
	fi
fi

if [ ! -f "${QEMU_SYSTEM_BINARY}" ]; then
	log_error "qemu-system-x86_64 binary does not exist. Run 'build_lib.sh qemu' in /path/to/lib/"
	exit 2
fi

if [ ! -f "${BZIMAGE_PATH}" ]; then
	log_error "SMDK kernel image does not exist. Run 'build_lib.sh kernel' in /path/to/lib/"
	exit 2
fi

if [ ! -f "${IMAGE_PATH}" ]; then
	log_error "QEMU rootfs ${IMAGE_PATH} does not exist. Run 'create_rootfs.sh' in /path/to/lib/qemu/"
	exit 2
fi

${QEMU_SYSTEM_BINARY} \
    -S -s -smp 4  \
    -numa node,cpus=0,memdev=mem0,nodeid=0 \
    -object memory-backend-ram,id=mem0,size=8G \
	-numa node,cpus=1,memdev=mem1,nodeid=1 \
    -object memory-backend-ram,id=mem1,size=8G \
	-numa node,cpus=2,memdev=mem2,nodeid=2 \
    -object memory-backend-ram,id=mem2,size=8G \
	-numa node,cpus=3,memdev=mem3,nodeid=3 \
    -object memory-backend-ram,id=mem3,size=8G \
    -kernel ${BZIMAGE_PATH} \
	-initrd ${INITRD_PATH} \
    -drive file=${IMAGE_PATH},index=0,media=disk,format=raw \
    -serial mon:stdio \
    -nographic \
    -append "root=/dev/sda rw console=ttyS0 memblock=debug loglevel=7 cgroup_no_v1=1" \
    -m 32G,slots=4,maxmem=36G \
   -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:22
 