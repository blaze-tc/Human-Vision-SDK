# Pinned Python image and vendor wheel; build context is out/rknn-toolchain.
# Keep this conversion environment separate from SDK/player dependencies.
FROM python:3.10-bookworm@sha256:f72a86806f92fe13a49f49aeca1db0b19ea5cf4a0470b842a01b0e14314945b8
RUN apt-get update && apt-get install -y --no-install-recommends libgl1 libglib2.0-0
COPY requirements.lock.txt /opt/rknn/requirements.lock.txt
COPY rknn_toolkit2-2.3.2-cp310-cp310-manylinux_2_17_x86_64.manylinux2014_x86_64.whl /opt/rknn/
RUN echo '6cb783ddf293ac509f39bf9127acf6a5492bbb67e4b4b4ac33a7c6d2cefb4f3c  /opt/rknn/rknn_toolkit2-2.3.2-cp310-cp310-manylinux_2_17_x86_64.manylinux2014_x86_64.whl' | sha256sum -c - \
    && pip install --extra-index-url https://download.pytorch.org/whl/cpu -r /opt/rknn/requirements.lock.txt \
    && pip install /opt/rknn/*.whl \
    && pip check
WORKDIR /workspace
ENV PYTHONUNBUFFERED=1
