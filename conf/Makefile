PREFIX	?=	 /usr/local
TOP_DIR	?=	$(shell readlink -f ..)

all::
clean::;	rm -f *~
veryclean::	clean
install::
	@mkdir -p $(PREFIX)/share/$$(basename $(TOP_DIR))
	cp -fp *.json $(PREFIX)/share/$$(basename $(TOP_DIR))
