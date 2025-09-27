# syntax=docker/dockerfile:1.5
ARG OMNIGRES_REV=master
ARG PG_VERSION=17.6
ARG BUILD=6.44
ARG BASE=registry.gitlab.com/ongresinc/builder/oci/postgres-build:v17.6-build-${BUILD}
ARG BUILD_PARALLEL_LEVEL=1
ARG OMNIGRES_REPO=https://github.com/omnigres/omnigres

FROM ${BASE}
USER root
# Get necessary dependendcies
RUN microdnf makecache
RUN microdnf install git cmake clang cpan openssl3-devel openssl3-libs python3-devel openssl bison flex readline-devel zlib-devel netcat
ARG PG_VERSION
ENV PG_VERSION=${PG_VERSION}
# Clone Omnigres
ARG OMNIGRES_REV
ENV OMNIGRES_REV=${OMNIGRES_REV}
ARG OMNIGRES_REPO
ENV OMNIGRES_REPO=${OMNIGRES_REPO}
RUN git clone --recursive ${OMNIGRES_REPO} /omnigres
RUN git -C /omnigres checkout ${OMNIGRES_REV}
# Build Omnigres
ARG BUILD_PARALLEL_LEVEL
ENV BUILD_PARALLEL_LEVEL=${BUILD_PARALLEL_LEVEL}
# 1. We are going to use clang(19) as gcc is very old (gcc8)
# 2. We need to ensure gettid() is defined (cppgres relies on it but it isn't available before glibc 2.30, erroneously: https://github.com/cppgres/cppgres/pull/56)
#    Before we upgrade all instances of cppgres.hpp, work around this.
RUN find /omnigres/extensions/ -name "cppgres.hpp" -exec sed -i '/^#include <unistd\.h>/a#if defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30))\n#include <sys/syscall.h>\nstatic inline pid_t gettid() {\n    return static_cast<pid_t>(syscall(SYS_gettid));\n}\n#endif' {} \;
RUN cmake -S /omnigres -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++  \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPG_CONFIG=/usr/lib/postgresql/${PG_VERSION}/bin/pg_config \
    -DCMAKE_BUILD_PARALLEL_LEVEL=${BUILD_PARALLEL_LEVEL} -DOPENSSL_CONFIGURED=1 -B /omnigres/build \
    -DPostgreSQL_TARGET_PACKAGE_LIBRARY_DIR=/install/$(/usr/lib/postgresql/${PG_VERSION}/bin/pg_config --pkglibdir) \
    -DPostgreSQL_TARGET_EXTENSION_DIR=/install/$(/usr/lib/postgresql/${PG_VERSION}/bin/pg_config --sharedir)/extension
RUN cmake --build /omnigres/build --parallel --target all
RUN mkdir -p /install/$(/usr/lib/postgresql/${PG_VERSION}/bin/pg_config --pkglibdir)
RUN mkdir -p /install/$(/usr/lib/postgresql/${PG_VERSION}/bin/pg_config --sharedir)/extension
RUN cmake --build /omnigres/build --target install_extensions
RUN <<EOF
export OMNI_SO_FILE="omni--$(grep "^omni=" /omnigres/versions.txt | cut -d "=" -f2).so"
ln -s ./${OMNI_SO_FILE} /install/$(/usr/lib/postgresql/${PG_VERSION}/bin/pg_config --pkglibdir)/omni.so
unset OMNI_SO_FILE
EOF
ARG BUILD
ENV BUILD=${BUILD}
USER postgres
