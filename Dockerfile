# sudo docker buildx build . --output minified

# https://stackoverflow.com/a/54245466
ARG BUILD_ENV=wget

FROM python:3.10 AS source_wget
RUN apt-get update && apt-get install strace bc bison flex clang lld llvm -y
RUN wget -nv https://git.kernel.org/torvalds/t/linux-5.19-rc8.tar.gz -O- | tar zxf -

FROM python:3.10 AS source_copy
RUN apt-get update && apt-get install strace bc bison flex clang lld llvm -y
ONBUILD ADD ./minified linux-5.19-rc8

FROM source_${BUILD_ENV} AS source
ADD linux-genlist.py .
RUN cd linux-5.19-rc8 && python3 ../linux-genlist.py
FROM scratch
COPY --from=source /linux-5.19-rc8 .
