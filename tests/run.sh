#!/usr/bin/env bash

MODE=""
PARALLEL_GC_THREADS=()
STRIPE_SIZE=32768

JAVA="$(pwd)/../jdk17u067/build/linux-x86_64-server-fastdebug/jdk/bin/java"
#JAVA="$(pwd)/../jdk17u067/build/linux-x86_64-server-release/jdk/bin/java"

EXEC=("Array" "Array_List" "Array_List_Int" "List_Large" "MultiList" \
	"Simple_Lambda" "Extend_Lambda" "Test_Reflection" "Test_Reference" \
	"HashMap" "Rehashing" "Clone" "Groupping" "MultiHashMap" \
	"Test_WeakHashMap" "ClassInstance")

EXEC=("Array_List")

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
    -XX:TeraHeapPolicy="SparkPrimitivePolicy" \
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
    -XX:TeraHeapPolicy="SparkPrimitivePolicy" \
    -Xlogth:llarge_teraCache.txt "${class_file}"
}

# Usage
usage() {
  echo
  echo "Usage:"
  echo -n "      $0 [option ...] [-h]"
  echo
  echo "Options:"
  echo "      -m  Mode (0: Default, 1: Interpreter, 2: C1, 3: C2, 4: gdb, 5: ShowMessageBoxOnError)"
  echo "      -t  Number of GC threads (2, 4, 8, 16, 32)"
  echo "      -h  Show usage"
  echo

  exit 1
}

check_args() {
  # Check if required options are present
  if [[ -z "$MODE" || "${#PARALLEL_GC_THREADS[@]}" -eq 0 ]]; then
    echo "Usage: $0 -m <mode> -t <#gcThreads,#gcThreads,#gcThreads> [-h]"
    exit 1
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
while getopts "m:t:h" opt
do
  case "${opt}" in
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
