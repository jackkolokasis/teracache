## TeraHeap: Reducing Memory Pressure in Managed Big Data Frameworks

### Description

TeraHeap extends the managed runtime (JVM) to use a second,
high-capacity heap over a fast storage device that coexists with the
regular heap. TeraHeap provides direct access to objects on the second
heap (no S/D). It also reduces GC cost by fencing the garbage
collector from scanning the second heap. TeraHeap leverages
frameworks’ property of choosing specific objects for off-heap
placement and offers frameworks a hint-based interface for moving
such objects to the second heap. 

### Install Prerequisites
Install the following packages:
```sh
sudo yum install python3-pip
pip3 install scan-build --user
pip3 install compdb --user
```
OpeJDK1.8 can be compiled with OpenJDK1.8. So, you need first to download
OpenJDK1.8.
```sh
# For Ubuntu
sudo apt-get install openjdk-8-jdk

# For Centos
sudo yum install java-1.8.0-openjdk
```

OpeJDK17 can be compiled with OpenJDK17. So, you need first to download
OpenJDK17.
```sh
# For Ubuntu
sudo apt-get install openjdk-17-jdk
```

Edit the compile.sh script in jdk17u067/ directory by changing the
JAVA_HOME variable to point to the installation directory of openjdk17 as follows:

```sh
export JAVA_HOME="/path/to/openjdk17"
```

OpenJDK8 can be compiled only with gcc <= 8.5 and g++ <= 8.5.
OpenJDK17 can be compiled only with gcc >= 8.5.

### Build
1. Build allocator.
```sh
cd allocator
./build.sh
cd -
```
Read the README.md file in allocator directory to export the specific
environment variables

2. Build tera_malloc.
```sh
cd tera_malloc
./build.sh
cd -
```
Read the README.md file in tera_malloc directory to export the
specific environment variables

3. Set your gcc/g++ path/alias 
```sh
cd ./jdk8u345 # for building java8
cd ./jdk17u067 # for building java17
```
and set CC and CXX variables inside compile.sh to your gcc path/alias

4. Build JVM (release mode)
```sh
./compile.sh -r
cd -
```

or

Build JVM (fastdebug mode)
```sh
./compile.sh -d
cd -
```

## Benchmarks
To run benchmarks please clone the repository
[tera_applications](https://github.com/jackkolokasis/tera_applications)
and read the README.md files in each application directory. There are
instructions about how to compile and run the different applications.

## Acknowledgements
We thank the anonymous reviewers for their insightful comments and
their help in preparing the final version of the paper. We
thankfully acknowledge the support of the European Commission under
the Horizon 2020 Framework Programme for Research and Innovation
through the projects EVOLVE (grant agreement No 825061). This
research is also partly supported by project EUPEX, which has
received funding from the European High-Performance Computing Joint
Undertaking (JU) under grant agreement No 101033975. The JU receives
support from the European Union's Horizon 2020 re-search and
innovation programme and France, Germany, Italy, Greece, United
Kingdom, Czech Republic, Croatia. Iacovos G. Kolokasis is also
supported by the Meta Research PhD Fellowship and the State
Scholarship Foundation of Cyprus.  

