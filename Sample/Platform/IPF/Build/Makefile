#/*++
#
#  Copyright (c) 2004 - 2008, Intel Corporation                                                         
#  All rights reserved. This program and the accompanying materials                          
#  are licensed and made available under the terms and conditions of the BSD License         
#  which accompanies this distribution.  The full text of the license may be found at        
#  http://opensource.org/licenses/bsd-license.php                                            
#                                                                                            
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
#  
#  Module Name:
#  
#    Makefile
#  
#  Abstract:
#   
#    This makefile is used to build the IPF build tip.
#   
#--*/

#
# Everything depends on EDK_SOURCE & EDK_TOOLS_PATH. Make sure it's defined
#
!IFNDEF EDK_SOURCE
!ERROR EDK_SOURCE environmental variable not set 
!ENDIF

!IFNDEF EDK_TOOLS_PATH
!MESSAGE EDK_TOOLS_PATH environmental variable not set, default setting used
!ENDIF

#
# Set the processor type
#
PROCESSOR = IPF

#
# Define a toolchain so we can pick up the basic defines from
# the EFI tool setup file.
#
TOOLCHAIN = TOOLCHAIN_MSVC

!INCLUDE PlatformTools.env

#
# Target to build customer config
#
all : tempstamp_file build_tools builds

#
# Create a file to store the date and time, and used by SetStamp.exe
#
tempstamp_file:
!IF DEFINED(DATE_STAMP) && DEFINED(TIME_STAMP)
  @echo $(DATE_STAMP) $(TIME_STAMP) > GenStamp.txt
!ELSEIF EXIST(GenStamp.txt)
  - @del GenStamp.txt
!ENDIF

#
# Make all the EFI build tools by calling the makefile
#
build_tools :
  -if not exist $(EDK_TOOLS_OUTPUT) mkdir $(EDK_TOOLS_OUTPUT)
  -if exist $(EDK_TOOLS_BIN)\*.* copy $(EDK_TOOLS_BIN)\*.* $(EDK_TOOLS_OUTPUT)
  $(MAKE) -f $(EDK_TOOLS_SOURCE)\makefile PROCESSOR=$(PROCESSOR) BUILD_DIR=$(BUILD_DIR)
  $(GUIDCHK) -b $(EDK_TOOLS_OUTPUT)\GuidDatabase.txt

COMMON_PROCESSDSC_OPTIONS =                        \
    -d EFI_SOURCE=$(EDK_SOURCE)                    \
    -d EDK_SOURCE=$(EDK_SOURCE)                    \
    -d PROCESSOR=$(PROCESSOR)                      \
    -d PROJECT_NAME=$(PROJECT_NAME)                \
    -d OEMTOOLPATH=$(EDK_TOOLS_OUTPUT)             \
    -d COMPRESS_METHOD=$(COMPRESS_METHOD)          \
    -d UEFI_PREFIX=$(UEFI_PREFIX)                  \
    -d PI_PREFIX=$(PI_PREFIX)                      \
    -g $(EDK_TOOLS_OUTPUT)\GuidDatabase.txt

#
# Run the utility that processes our description files and generates the
# makefiles. Specify -v option for more verbose output.
# Specify "-xref filename" to generate a cross-reference file of 
# basename-to-fileguid that can be used by VolInfo.
#
makefiles : $(PROCESSOR)\Blank.pad
  $(PROCESSDSC) $(COMMON_PROCESSDSC_OPTIONS) $(PROJECT_NAME).dsc

#
# Multi-thread build using ProcessDsc utility.
#
builds : $(PROCESSOR)\Blank.pad
  $(PROCESSDSC) $(COMMON_PROCESSDSC_OPTIONS) -n $(BUILD_THREAD_NUMBER) \
                -t libraries -t components $(PROJECT_NAME).dsc

#
# Copy blank pad section file the bin directory
#

$(PROCESSOR)\Blank.pad : Blank.pad
  if not exist $(PROCESSOR) md $(PROCESSOR)
  copy $** $@
  

#
# Create a target to clean up all build directories
#
clean : cleantools cleanbuilds

#
# Clean up all build directories except for tools.
#
cleanbuilds :
  - @if exist *.bak del *.bak > NUL
  - @if exist $(PROCESSOR) rd /s /q $(PROCESSOR) > NUL
  - @if exist ebc rd /s /q ebc > NUL
  - @if exist fv rd /s /q fv > NUL
  - @if exist log rd /s /q log > NUL
  - @if exist makefile.out del makefile.out > NUL
  - @if exist module.* del module.* > NUL
  - @echo All modules cleaned

#
# Clean up only the tools
#
cleantools :
  - @if exist vc??.* del vc??.* > NUL
  - @if exist Tools rd /s /q Tools > NUL
  - @echo All tools cleaned

#
# Rebuild target, clean and build all
#
rebuild : clean all

#
# cleanall target to clean all build directories
# and binary files in $(EFI_PLATFORM_BIN).
#
cleanall : cleantools cleanbuilds
  - @if exist $(EFI_PLATFORM_BIN) rd /s /q $(EFI_PLATFORM_BIN) > NUL
  - @if exist $(EFI_BINARY) rd $(EFI_BINARY) > NUL 2>&1  
  - @echo All targets cleaned
