FROM archlinux/archlinux:base-devel

RUN mkdir -p /etc/pacman.d/hooks \
	&& ln -s /dev/null /etc/pacman.d/hooks/30-systemd-tmpfiles.hook

RUN echo "Server = http://mirror.yandex.ru/archlinux/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist

RUN pacman -Syu --noconfirm \
	&& pacman -S --needed --noconfirm \
		vim \
		git \
		libjpeg \
		libevent \
		libutil-linux \
		libbsd \
		python \
		python-pip \
		python-tox \
		cppcheck \
		npm \
	&& (pacman -Sc --noconfirm || true) \
	&& rm -rf /var/cache/pacman/pkg/*

RUN npm install htmlhint -g

CMD /bin/bash
