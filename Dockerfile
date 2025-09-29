### HELPER IMAGE LAYER -
FROM ubuntu:22.04 as vision-builder

SHELL ["/bin/bash", "-c"]

ENV	DEBIAN_FRONTEND="noninteractive"

WORKDIR	/src

RUN	apt update -yq \
	&& apt install -yqq \
	build-essential cmake git libsqlite3-dev libcrypto++-dev libfaac-dev ca-certificates musl

RUN	cd /src \
	&& git clone -b dtls_srtp_support --depth=1 https://github.com/livepeer/mbedtls.git \
	&& cd mbedtls \
	&& mkdir build \
	&& cd build \
	&& cmake \
		-D CMAKE_BUILD_TYPE=Release \
		-D CMAKE_INSTALL_PREFIX=/usr/local \
		-D CMAKE_C_FLAGS="-fPIC" \
		.. \
	&& make -j$(nproc) install

RUN cd /src \
	&& git clone https://github.com/cisco/libsrtp.git \
	&& cd libsrtp \
	&& git checkout fd08747fa6800 \
	&& mkdir build \
	&& cd build \
	&& cmake \
		-D CMAKE_BUILD_TYPE=Release \
		-D CMAKE_INSTALL_PREFIX=/usr/local \
		-D CMAKE_C_FLAGS="-fPIC" \
		.. \
	&& make -j$(nproc) install

RUN cd /src \
	&& git clone --depth=1 --branch 0.9.5.0 https://github.com/sctplab/usrsctp.git \
	&& cd usrsctp \
	&& mkdir build \
	&& cd build \
	&& cmake \
		-D CMAKE_BUILD_TYPE=Release \
		-D CMAKE_INSTALL_PREFIX=/usr/local \
		-D CMAKE_POSITION_INDEPENDENT_CODE=ON \
		-D sctp_build_shared_lib=OFF \
		-D sctp_build_programs=OFF \
		.. \
	&& make -j$(nproc) install

RUN echo "Additionally, install srt (using their git) and tcmalloc (using apt package) if needed"

WORKDIR	/src

COPY . .

WORKDIR	/src/build

ARG CONTROL_VERSION="v1"
ARG	BUILD_VERSION="RTMPServer:${CONTROL_VERSION}"
ENV	BUILD_VERSION="${BUILD_VERSION}"
ARG TARGETARCH
ARG DEB_SERVER

RUN	mkdir -p /src/build/ \
	&& cd /src/build/ \
	&& echo "${BUILD_VERSION}" > BUILD_VERSION \
	&& cmake \
		-D NOLLHLS=1 \
		-D APPNAME=RTMPServer \
		-D UDP_API_PORT=4240 \
		-D WITH_THREADNAMES=1 \
		-D CMAKE_INSTALL_PREFIX=/usr/local \
		-D CMAKE_BUILD_TYPE=Release \
		-D NOAUTH=1 \
		-D DEBUG= 0 \
		.. \
	&& make -j$(nproc) install

RUN find /usr/local/bin /usr/local/lib -type f -executable -exec strip -s {} \+ \
	&& rm -rf /usr/local/lib /usr/local/include

### PRODUCTION IMAGE LAYER -
FROM ubuntu:22.04

RUN apt update -yq \
	&& apt install -yqq curl ffmpeg lsof supervisor jq \
	&& rm -rf /var/lib/apt/lists/* \
	&& apt autoremove -yqq

COPY --from=vision-builder /usr/local/bin/ /usr/local/bin/
RUN	find /usr/local/bin -maxdepth 1 -type f ! -name 'Mist*' -exec rm -f {} +
COPY entrypoint.sh /entrypoint.sh
COPY healthcheck.sh /healthcheck.sh
COPY restartcontroller.sh /restartcontroller.sh
COPY configs/supervisord.conf /etc/supervisor/conf.d/supervisord.conf

RUN chmod 777 /entrypoint.sh /healthcheck.sh /restartcontroller.sh

# RTMP, API, HTTP, RTSP, HLS
EXPOSE 1935 4242 8080 5554 8081
# HEALTHCHECK --start-period=60s --interval=120s --timeout=300s CMD /healthcheck.sh || exit 1
ENTRYPOINT	["/bin/bash", "-c", "/entrypoint.sh"]
