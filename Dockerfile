# sudo docker buildx build . --output minified
FROM python:3.10 AS source
RUN apt-get update && apt-get install strace bc bison flex -y
RUN wget -nv https://git.kernel.org/torvalds/t/linux-5.19-rc8.tar.gz -O- | tar zxf -
#ADD ./minified linux-5.19-rc8
ADD linux-genlist.py .
RUN cd linux-5.19-rc8 && python3 ../linux-genlist.py
FROM scratch
COPY --from=source /linux-5.19-rc8 .
