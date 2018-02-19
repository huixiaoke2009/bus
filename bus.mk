##
## Auto Generated makefile by CodeLite IDE
## any manual changes will be erased      
##
## Debug
ProjectName            :=bus
ConfigurationName      :=Debug
WorkspacePath          :=/Users/chandahu/Documents/workspace/chandahu
ProjectPath            :=/Users/chandahu/Documents/workspace/chandahu/bus
IntermediateDirectory  :=./Debug
OutDir                 := $(IntermediateDirectory)
CurrentFileName        :=
CurrentFilePath        :=
CurrentFileFullPath    :=
User                   :=chandahu
Date                   :=14/02/2018
CodeLitePath           :="/Users/chandahu/Library/Application Support/CodeLite"
LinkerName             :=/usr/bin/g++
SharedObjectLinkerName :=/usr/bin/g++ -dynamiclib -fPIC
ObjectSuffix           :=.o
DependSuffix           :=.o.d
PreprocessSuffix       :=.i
DebugSwitch            :=-g 
IncludeSwitch          :=-I
LibrarySwitch          :=-l
OutputSwitch           :=-o 
LibraryPathSwitch      :=-L
PreprocessorSwitch     :=-D
SourceSwitch           :=-c 
OutputFile             :=$(IntermediateDirectory)/$(ProjectName)
Preprocessors          :=
ObjectSwitch           :=-o 
ArchiveOutputSwitch    := 
PreprocessOnlySwitch   :=-E
ObjectsFileList        :="bus.txt"
PCHCompileFlags        :=
MakeDirCommand         :=mkdir -p
LinkOptions            :=  
IncludePath            :=  $(IncludeSwitch). $(IncludeSwitch). 
IncludePCH             := 
RcIncludePath          := 
Libs                   := 
ArLibs                 :=  
LibPath                := $(LibraryPathSwitch). 

##
## Common variables
## AR, CXX, CC, AS, CXXFLAGS and CFLAGS can be overriden using an environment variables
##
AR       := /usr/bin/ar rcu
CXX      := /usr/bin/g++
CC       := /usr/bin/gcc
CXXFLAGS :=  -g -O0 -Wall $(Preprocessors)
CFLAGS   :=  -g -O0 -Wall $(Preprocessors)
ASFLAGS  := 
AS       := /usr/bin/as


##
## User defined environment variables
##
CodeLiteDir:=/Applications/codelite.app/Contents/SharedSupport/
Srcs=logsvr/logsvr_proc.cpp auth/auth_proc.cpp bus/bus.cpp user/writer_user.cpp mmlib/udp_client/udp_client.cpp mmlib/util/util.cpp user/user_main.cpp user/user_shm_api.cpp user/loader_user.cpp conn/conn_main.cpp \
	conn/conn.cpp include/filelock.cpp mmlib/sem_lock/sem_lock.cpp mmlib/ini_file/ini_file.cpp mmlib/amf/amf3serializer.cpp bus/bus_main.cpp mmlib/safe_tcp_client/safe_tcp_client.cpp mmlib/mmap_file/mmap_file.cpp auth/auth_main.cpp mmlib/share_mem/share_mem.cpp \
	mmlib/process_manager/process_manager.cpp mmlib/shm_queue/shm_uniq_queue.cpp mmlib/quicklz/quicklz.c mmlib/simple_uuid64/simple_uuid64.cpp mmlib/mysql/mysql_wrap.cpp mmlib/amf/amflog.cpp mmlib/gzip/gzip.cpp mmlib/base64/base64.cpp mmlib/mmap_queue/mmap_queue.cpp mmlib/sem_lock/sem_lock_pro.cpp \
	mmlib/shm_queue/shm_queue.cpp user/user_proc.cpp mmlib/amf/variant.cpp mmlib/log/log.cpp mmlib/file_lock/file_lock.cpp mmlib/md5/md5.cpp mmlib/amf/amf0serializer.cpp mmlib/tcp_client/tcp_client.cpp logsvr/logsvr_main.cpp mmlib/amf/iobuffer.cpp \
	mmlib/amf/common.cpp 

Objects0=$(IntermediateDirectory)/logsvr_logsvr_proc.cpp$(ObjectSuffix) $(IntermediateDirectory)/auth_auth_proc.cpp$(ObjectSuffix) $(IntermediateDirectory)/bus_bus.cpp$(ObjectSuffix) $(IntermediateDirectory)/user_writer_user.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_udp_client_udp_client.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_util_util.cpp$(ObjectSuffix) $(IntermediateDirectory)/user_user_main.cpp$(ObjectSuffix) $(IntermediateDirectory)/user_user_shm_api.cpp$(ObjectSuffix) $(IntermediateDirectory)/user_loader_user.cpp$(ObjectSuffix) $(IntermediateDirectory)/conn_conn_main.cpp$(ObjectSuffix) \
	$(IntermediateDirectory)/conn_conn.cpp$(ObjectSuffix) $(IntermediateDirectory)/include_filelock.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_sem_lock_sem_lock.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_ini_file_ini_file.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_amf_amf3serializer.cpp$(ObjectSuffix) $(IntermediateDirectory)/bus_bus_main.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_safe_tcp_client_safe_tcp_client.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_mmap_file_mmap_file.cpp$(ObjectSuffix) $(IntermediateDirectory)/auth_auth_main.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_share_mem_share_mem.cpp$(ObjectSuffix) \
	$(IntermediateDirectory)/mmlib_process_manager_process_manager.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_shm_queue_shm_uniq_queue.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_quicklz_quicklz.c$(ObjectSuffix) $(IntermediateDirectory)/mmlib_simple_uuid64_simple_uuid64.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_mysql_mysql_wrap.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_amf_amflog.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_gzip_gzip.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_base64_base64.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_mmap_queue_mmap_queue.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_sem_lock_sem_lock_pro.cpp$(ObjectSuffix) \
	$(IntermediateDirectory)/mmlib_shm_queue_shm_queue.cpp$(ObjectSuffix) $(IntermediateDirectory)/user_user_proc.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_amf_variant.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_log_log.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_file_lock_file_lock.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_md5_md5.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_amf_amf0serializer.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_tcp_client_tcp_client.cpp$(ObjectSuffix) $(IntermediateDirectory)/logsvr_logsvr_main.cpp$(ObjectSuffix) $(IntermediateDirectory)/mmlib_amf_iobuffer.cpp$(ObjectSuffix) \
	$(IntermediateDirectory)/mmlib_amf_common.cpp$(ObjectSuffix) 



Objects=$(Objects0) 

##
## Main Build Targets 
##
.PHONY: all clean PreBuild PrePreBuild PostBuild MakeIntermediateDirs
all: $(OutputFile)

$(OutputFile): $(IntermediateDirectory)/.d $(Objects) 
	@$(MakeDirCommand) $(@D)
	@echo "" > $(IntermediateDirectory)/.d
	@echo $(Objects0)  > $(ObjectsFileList)
	$(LinkerName) $(OutputSwitch)$(OutputFile) @$(ObjectsFileList) $(LibPath) $(Libs) $(LinkOptions)

MakeIntermediateDirs:
	@test -d ./Debug || $(MakeDirCommand) ./Debug


$(IntermediateDirectory)/.d:
	@test -d ./Debug || $(MakeDirCommand) ./Debug

PreBuild:
##
## Clean
##
clean:
	$(RM) -r ./Debug/


