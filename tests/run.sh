#!/usr/bin/env bash

MODE=""
PARALLEL_GC_THREADS=()
STRIPE_SIZE=32768
jvm_build=""
cpu_arch=$(uname -p)

EXEC=("Array" "Array_List" "Array_List_Int" "List_Large" "MultiList" \
	"Simple_Lambda" "Extend_Lambda" "Test_Reflection" "Test_Reference" \
	"HashMap" "Rehashing" "Clone" "Groupping" "MultiHashMap" \
	"Test_WeakHashMap" "ClassInstance")

# Export Enviroment Variables
export_env_vars() {
	PROJECT_DIR="$(pwd)/../.."

	export LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/allocator/lib/:$LD_LIBRARY_PATH
	export PATH=${PROJECT_DIR}/allocator/include/:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$C_INCLUDE_PATH
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/allocator/include/:$CPLUS_INCLUDE_PATH
	
  export LIBRARY_PATH=${PROJECT_DIR}/tera_malloc/lib/:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/tera_malloc/lib/:$LD_LIBRARY_PATH
	export PATH=${PROJECT_DIR}/tera_malloc/include/:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/tera_malloc/include/:$C_INCLUDE_PATH
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/tera_malloc/include/:$CPLUS_INCLUDE_PATH
}

# Run tests using only interpreter mode
function interpreter_mode() {
  local class_file=$1
  local num_gc_thread=$2

	${JAVA} -server \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly -XX:+PrintInterpreter -XX:+PrintNMethods \
		-Djava.compiler=NONE \
		-XX:+ShowMessageBoxOnError \
		-XX:+UseParallelGC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:-UseCompressedClassPointers \
		-XX:+TeraHeapStatistics \
		-XX:TeraStripeSize=${STRIPE_SIZE} \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
		-Xlogth:llarge_teraCache.txt "${class_file}" > err 2>&1 > out
}

# Run tests using only C1 compiler
function c1_mode() {
  local class_file=$1
  local num_gc_thread=$2

	 ${JAVA} \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly \
		-XX:+PrintInterpreter \
		-XX:+PrintNMethods -XX:+PrintCompilation \
		-XX:+ShowMessageBoxOnError -XX:+LogCompilation \
		-XX:TieredStopAtLevel=3\
		-XX:+UseParallelGC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:-UseParallelOldGC \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:+TeraHeapStatistics \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
		-Xlogth:llarge_teraCache.txt "${class_file}" > err 2>&1 > out
}
	 
# Run tests using C2 compiler
function c2_mode() {
  local class_file=$1
  local num_gc_thread=$2

	 ${JAVA} \
		 -server \
		-XX:+UnlockDiagnosticVMOptions -XX:+PrintAssembly \
		-XX:+PrintNMethods -XX:+PrintCompilation \
		-XX:+ShowMessageBoxOnError -XX:+LogCompilation \
		-XX:+UseParallelGC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:-UseParallelOldGC \
		-XX:+EnableTeraCache \
		-XX:TeraCacheSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:TeraCacheThreshold=0 \
		-XX:-UseCompressedOops \
		-XX:+TeraCacheStatistics \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
		-Xlogtc:llarge_teraCache.txt "${class_file}" > err 2>&1 > out
} 

# Run tests using all compilers
function run_tests_msg_box() {
  local class_file=$1
  local num_gc_thread=$2

	${JAVA} \
		-server \
		-XX:+ShowMessageBoxOnError \
		-XX:+UseParallelGC \
		-XX:ParallelGCThreads=${num_gc_thread} \
		-XX:+EnableTeraHeap \
		-XX:TeraHeapSize=${TERACACHE_SIZE} \
		-Xmx${MAX}g \
		-Xms${XMS}g \
		-XX:-UseCompressedOops \
		-XX:-UseCompressedClassPointers \
		-XX:+TeraHeapStatistics \
		-XX:TeraStripeSize=${STRIPE_SIZE} \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
		-Xlogth:llarge_teraCache.txt "${class_file}" > err 2>&1 > out
}

# Run tests using all compilers
function run_tests() {
  local class_file=$1
  local num_gc_thread=$2

  ${JAVA} \
    -server \
    -XX:+UseParallelGC \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:+EnableTeraHeap \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:-UseCompressedOops \
    -XX:-UseCompressedClassPointers \
    -XX:+TeraHeapStatistics \
    -XX:TeraStripeSize=${STRIPE_SIZE} \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
    -Xlogth:llarge_teraCache.txt "${class_file}" > err 2>&1 > out
  }

# Run tests using gdb
function run_tests_debug() {
  local class_file=$1
  local num_gc_thread=$2

  gdb --args ${JAVA} \
    -server \
    -XX:+ShowMessageBoxOnError \
    -XX:+UseParallelGC \
    -XX:ParallelGCThreads=${num_gc_thread} \
    -XX:+EnableTeraHeap \
    -XX:TeraHeapSize=${TERACACHE_SIZE} \
    -Xmx${MAX}g \
    -Xms${XMS}g \
    -XX:-UseCompressedOops \
    -XX:-UseCompressedClassPointers \
    -XX:+TeraHeapStatistics \
    -XX:TeraStripeSize=${STRIPE_SIZE} \
    -XX:AllocateH2At="/mnt/fmap/" \
    -XX:H2FileSize=751619276800 \
    -Xlogth:llarge_teraCache.txt "${class_file}"
}

# Usage
usage() {
  echo
  echo "Usage:"
  echo -n "      $0 [option ...] [-h]"
  echo
  echo "Options:"
  echo "      -j  jvm build ([release|r], [fastdebug|f], Default: release)"
  echo "      -m  Mode (0: Default, 1: Interpreter, 2: C1, 3: C2, 4: gdb, 5: ShowMessageBoxOnError)"
  echo "      -t  Number of GC threads (2, 4, 8, 16, 32)"
  echo "      -h  Show usage"
  echo

  exit 1
}

check_args() {
  # Check if required options are present
  if [[ -z "$MODE" || "${#PARALLEL_GC_THREADS[@]}" -eq 0 ]]; then
    echo "Usage: $0 -j <jvm_build> -m <mode> -t <#gcThreads,#gcThreads,#gcThreads> [-h]"
    echo "Example: ./run.sh -j release -m 0 -t 2 -r //execute a release jvm variant(-j release) using tiered compilation(-m 0) and 2 GC threads(-t 2)"
    echo "Example: ./run.sh -j fastdebug -m 0 -t 2 -d //execute a fastdebug jvm variant(-j fastdebug) using tiered compilation(-m 0) and 2 GC threads(-t 2)"
    exit 1
  fi
}

check_jvm_build() {
	# Validate jvm_build value
	if [[ "$jvm_build" != "" && "$jvm_build" != "fastdebug" && "$jvm_build" != "f" && "$jvm_build" != "release" && "$jvm_build" != "r" ]]; then
	    echo "Error: jvm_build should be empty(for vanilla) or one of: fastdebug, f, release, r"
	    usage
	    exit 1
	fi
	# Use the appropriate java binary based on jvm_build
	if [[ "$jvm_build" == "fastdebug" || "$jvm_build" == "f" ]]; then
	    JAVA="$(pwd)/../jdk17u067/build/linux-$cpu_arch-server-fastdebug/jdk/bin/java"
	elif [[ "$jvm_build" == "release" || "$jvm_build" == "r" ]]; then
	    JAVA="$(pwd)/../jdk17u067/build/linux-$cpu_arch-server-release/jdk/bin/java"
	fi
}

print_msg() {
  local gcThread=$1
  local mode_value
  case "${MODE}" in
    0)
      mode_value="Default"
      ;;
    1)
      mode_value="Interpreter"
      ;;
    2)
      mode_value="C1"
      ;;
    3)
      mode_value="C2"
      ;;
  esac

  echo "___________________________________"
  echo 
  echo "         Run JAVA Tests"
  echo 
  echo "Mode:       ${mode_value}"
  echo "GC Threads: ${gcThread}"
  echo "___________________________________"
  echo 
}

# Check for the input arguments
while getopts "j:m:t:h" opt
do
  case "${opt}" in
    j)
      jvm_build=${OPTARG}
      check_jvm_build
      ;;
    m)
      MODE=${OPTARG}
      ;;
    t)
      IFS=',' read -r -a PARALLEL_GC_THREADS <<< "$OPTARG"
      ;;
    h)
      usage
      ;;
    *)
      usage
      ;;
  esac
done

check_args

cd java || exit

for gcThread in "${PARALLEL_GC_THREADS[@]}"
do
  print_msg "$gcThread"

  for exec_file in "${EXEC[@]}"
  do
    if [ "${exec_file}" == "ClassInstance" ]
    then
      XMS=2
    elif [ "${exec_file}" == "Array_List" ]
    then
      XMS=3
    else
      XMS=1
    fi

    MAX=100
    TERACACHE_SIZE=$(echo $(( (MAX-XMS)*1024*1024*1024 )))
    case ${MODE} in
      0)
        export_env_vars
        run_tests "$exec_file" "$gcThread"
        ;;
      1)
        export_env_vars
        interpreter_mode "$exec_file" "$gcThread"
        ;;
      2)
        export_env_vars
        c1_mode "$exec_file" "$gcThread"
        ;;
      3)
        export_env_vars
        c2_mode "$exec_file" "$gcThread"
        ;;
      4)
        export_env_vars
        run_tests_debug "$exec_file" "$gcThread"
        ;;
      5)
        export_env_vars
        run_tests_msg_box "$exec_file" "$gcThread"
        ;;
    esac

    ans=$?

    echo -ne "${exec_file} "

    if [ $ans -eq 0 ]
    then    
      echo -e '\e[30G \e[32;1mPASS\e[0m';    
    else    
      echo -e '\e[30G \e[31;1mFAIL\e[0m';    
      break
    fi    
  done

done

cd - > /dev/null || exit
