#
#  Copyright 2009-2015, Jack Poulson
#  All rights reserved.
#
#  This file is part of Elemental and is under the BSD 2-Clause License,
#  which can be found in the LICENSE file in the root directory, or at
#  http://opensource.org/licenses/BSD-2-Clause
#
include(ExternalProject)

# NOTE: There is no test for MPICH since it will be assumed that tests/MPI
#       was already run (and did not find MPI)

# NOTE: This is untested and not yet used by Elemental, as querying for the 
#       flags used by MPICH is non-trivial and might be best hard-coded for
#       different architectures.

if(MSVC)
  set(EL_HAVE_MPICH FALSE)
  set(EL_BUILT_MPICH FALSE)
else()
  if(NOT DEFINED MPICH_URL)
    set(MPICH_URL http://www.mpich.org/static/downloads/3.1.4/mpich-3.1.4.tar.gz)
  endif()
  message(STATUS "Will download MPICH tarball from ${MPICH_URL}")

  set(MPICH_SOURCE_DIR ${PROJECT_BINARY_DIR}/download/mpich/source)
  set(MPICH_BINARY_DIR ${PROJECT_BINARY_DIR}/download/mpich/build)
  if(CMAKE_Fortran_COMPILER)
    set(TRY_TO_PASS_FC --FC=${CMAKE_Fortran_COMPILER})
  else()
    set(TRY_TO_PASS_FC --disable-fortran)
  endif()

  ExternalProject_Add(project_mpich
    PREFIX ${CMAKE_INSTALL_PREFIX}
    URL ${MPICH_URL}
    STAMP_DIR ${MPICH_BINARY_DIR}/stamp
    BUILD_IN_SOURCE 1
    SOURCE_DIR ${MPICH_SOURCE_DIR}
    TMP_DIR    ${MPICH_BINARY_DIR}/tmp
    INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
    CONFIGURE_COMMAND ${MPICH_SOURCE_DIR}/configure --prefix=<INSTALL_DIR> --CC=${CMAKE_C_COMPILER} --CXX=${CMAKE_CXX_COMPILER} ${TRY_TO_PASS_FC}
    UPDATE_COMMAND "" 
    BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}
    INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install
  )
  add_dependencies(External project_mpich)

  # Extract the installation directory
  ExternalProject_Get_Property(project_mpich install_dir)

  # Add targets for mpich (either shared or static)
  if(BUILD_SHARED_LIBS)
    # TODO: Fortran libraries?
    add_library(libmpicxx SHARED IMPORTED)
    add_library(libmpi SHARED IMPORTED)
    add_library(libpmpi SHARED IMPORTED)
    set_property(TARGET libmpicxx PROPERTY IMPORTED LOCATION ${install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}mpicxx${CMAKE_SHARED_LIBRARY_SUFFIX})
    set_property(TARGET libmpi PROPERTY IMPORTED_LOCATION ${install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}mpi${CMAKE_SHARED_LIBRARY_SUFFIX})
    set_property(TARGET libpmpi PROPERTY IMPORTED_LOCATION ${install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}pmpi${CMAKE_SHARED_LIBRARY_SUFFIX})
  else()
    # TODO: Fortran libraries?
    add_library(libmpicxx STATIC IMPORTED)
    add_library(libmpi STATIC IMPORTED)
    add_library(libpmpi STATIC IMPORTED)
    set_property(TARGET libmpicxx PROPERTY IMPORTED LOCATION ${install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}mpicxx${CMAKE_STATIC_LIBRARY_SUFFIX})
    set_property(TARGET libmpi PROPERTY IMPORTED_LOCATION ${install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}mpi${CMAKE_STATIC_LIBRARY_SUFFIX})
    set_property(TARGET libpmpi PROPERTY IMPORTED_LOCATION ${install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pmpi${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif() 

  set(MPI_C_LIBRARIES libmpi libpmpi)
  set(MPI_CXX_LIBRARIES libmpicxx libmpi libpmpi)
  set(EL_HAVE_MPICH TRUE)
  set(EL_BUILT_MPICH TRUE)
endif()
