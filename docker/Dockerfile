FROM rockylinux:8

# Install dependencies
RUN dnf -y update && \
    dnf -y install epel-release && \
    dnf -y install \
        gcc \
        gcc-c++ \
        make \
        pkgconfig \
        pcre-devel \
        tcl-devel \
        expat-devel \
        openssl-devel \
        libcurl-devel \
        perl-ExtUtils-MakeMaker \
        autoconf \
        automake \
        libtool \
        git \
        wget \
        bzip2 \
        lbzip2 \
        && dnf clean all

# Download and install Traffic Server
WORKDIR /usr/local/src
RUN wget https://archive.apache.org/dist/trafficserver/trafficserver-9.2.0.tar.bz2 && \
    tar xf trafficserver-9.2.0.tar.bz2 && \
    cd trafficserver-9.2.0 && \
    ./configure --prefix=/opt/ts && \
    make -j$(nproc) && \
    make install && \
    cd .. && \
    rm -rf trafficserver-9.2.0*

# Create necessary directories
RUN mkdir -p /opt/ts/etc/trafficserver && \
    mkdir -p /opt/ts/var/log/trafficserver && \
    mkdir -p /opt/ts/var/cache/trafficserver

# Copy configuration files
COPY config/records.yaml /opt/ts/etc/trafficserver/
COPY config/remap.config /opt/ts/etc/trafficserver/
COPY config/ip_allow.yaml /opt/ts/etc/trafficserver/

# Expose port
EXPOSE 8080

# Start Traffic Server
CMD ["/opt/ts/bin/traffic_server"]
