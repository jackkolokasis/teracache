###################################################
#
# file: compile.sh
#
# Iacovos G. Kolokasis
# @Version:  07-03-2021 
# @email:    kolokasis@ics.forth.gr
#
# Compile JVM
#
###################################################

function usage()
{
    echo
    echo "Usage:"
    echo -n "      $0 [option ...] [-h]"
    echo
    echo "Options:"
    echo "      -r  Build release without debug symbols"
    echo "      -d  Build with debug symbols"
    echo "      -c  Clean and make"
    echo "      -u  Update JVM in root directory"
    echo "      -h  Show usage"
    echo

    exit 1
}
# Compile without debug symbols
function release() 
{
  make dist-clean
  bash ./configure \
    --with-jobs="$(nproc)" \
    --with-extra-cflags="-O3 -I/spare/kd/teraheap/allocator/include -I/spare/kd/teraheap/allocator/include" \
    --with-extra-cxxflags="-O3 -I/spare/kd/teraheap/allocator/include -I/spare/kd/teraheap/allocator/include" \
    --with-target-bits=64
  
  intercept-build make
  cd ../ 
  compdb -p jdk17u067 list > compile_commands.json
  mv compile_commands.json jdk17u067
  cd - || exit
}

# Compile with debug symbols and assertions
function debug_symbols_on() 
{
	export LD_LIBRARY_PATH=/spare/kd/teraheap/allocator/lib:$LD_LIBRARY_PATH

  make dist-clean
  bash ./configure \
    --with-debug-level=fastdebug \
    --with-native-debug-symbols=internal \
    --with-target-bits=64 \
    --with-jobs="$(nproc)" \
    --with-extra-cflags="-I/spare/kd/teraheap/allocator/include -I/spare/kd/teraheap/allocator/include" \
    --with-extra-cxxflags="-I/spare/kd/teraheap/allocator/include -I/spare/kd/teraheap/allocator/include"

  intercept-build make
  cd ../ 
  compdb -p jdk17u067 list > compile_commands.json
  mv compile_commands.json jdk17u067
  cd - || exit
}

function clean_make()
{
  make clean
  make
}

export_env_vars()
{
	local PROJECT_DIR="$(pwd)/.."

	export JAVA_HOME="/home1/public/konstdelis/jvm/jdk17/build/linux-x86_64-server-release/jdk"

	### TeraHeap Allocator
	export LIBRARY_PATH=${PROJECT_DIR}/allocator/lib:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/allocator/lib:$LD_LIBRARY_PATH                                                                                           
	export PATH=${PROJECT_DIR}/allocator/include:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/allocator/include:$C_INCLUDE_PATH                                                                                         
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/allocator/include:$CPLUS_INCLUDE_PATH

	export LIBRARY_PATH=${PROJECT_DIR}/tera_malloc/lib:$LIBRARY_PATH
	export LD_LIBRARY_PATH=${PROJECT_DIR}/tera_malloc/lib:$LD_LIBRARY_PATH                                                                                           
	export PATH=${PROJECT_DIR}/tera_malloc/include:$PATH
	export C_INCLUDE_PATH=${PROJECT_DIR}/tera_malloc/include:$C_INCLUDE_PATH                                                                                         
	export CPLUS_INCLUDE_PATH=${PROJECT_DIR}/tera_malloc/include:$CPLUS_INCLUDE_PATH
}


while getopts ":drcmh" opt
do
  case "${opt}" in
    r)
      export_env_vars
      release
      ;;
    d)
      export_env_vars
      debug_symbols_on


      ;;
    c)
      export_env_vars
      clean_make
      ;;
    m)
      export_env_vars
      make
      ;;
    h)
      usage
      ;;
    *)
      usage
      ;;
  esac
done
