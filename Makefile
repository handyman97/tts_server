#

TOP_DIR	= $(shell readlink -f .)
PREFIX	?= /usr/local
SUBDIRS	= src conf scripts

all clean veryclean install::
	for d in $(SUBDIRS); do PREFIX=$(PREFIX) $(MAKE) -C $$d $@ || exit 1; done
clean::
	find . -name '*~' | xargs rm -f
veryclean::	clean

# docker
DOCKER_IMAGE	= handyman97/tts_server
$(DOCKER_IMAGE)-dev:
	@docker images | grep -q "^$@ " && { echo "$@ exists"; exit 0; } ||\
	docker build --target builder -t $@ .
$(DOCKER_IMAGE):
	@docker images | grep -q "^$@ " && { echo "$@ exists"; exit 0; } ||\
	docker build -t $@ .

docker-build:	$(DOCKER_IMAGE)
docker-run:	$(DOCKER_IMAGE)
#	docker run -it --rm $<
	docker run --expose 1883 $< $(PREFIX)/tts_server_start.sh

#
tar::	clean
	cwd=$(shell basename $$PWD); \
	filename=$${cwd}-$(shell date +%y%m%d).tar.xz; \
	cd ..; test -f $$filename ||\
	tar cvJf $$filename --exclude=obsolete --exclude=_build --exclude=node_modules $$cwd

#
rsync-to-%:	clean
	dest=src/2022/$(shell basename $$PWD);\
	rsync -avzop --exclude=node_modules \
	$(TOP_DIR)/ $*:$$dest
