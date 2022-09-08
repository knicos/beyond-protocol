FROM app.ftlab.utu.fi/base:1.8-dev

ARG USERNAME=user
ARG USER_UID=1000
ARG USER_GID=$USER_UID

# Create the user
RUN addgroup --gid $USER_GID user \
    && adduser --disabled-password --gecos '' --uid $USER_UID --gid $USER_GID user && usermod -a -G audio user && usermod -a -G video user \
    #
    # [Optional] Add sudo support. Omit if you don't need to install software after connecting.
    && apt-get update \
    && apt-get install -y sudo python3-pip file \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && pip install cpplint cppclean

# ********************************************************
# * Anything else you want to do like clean up goes here *
# ********************************************************

# [Optional] Set the default user. Omit if you want to keep the default as root.
USER $USERNAME