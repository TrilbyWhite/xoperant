
PREFIX	?=	/usr
PROG	=	xoperant
CC		?=	gcc
FLAGS	+=	${CFLAGS} ${LDFLAGS} -lX11 -lphidget21

${PROG}: ${PROG}.c
	@${CC} -o ${PROG} ${PROG}.c ${FLAGS}

install:
	@install -m755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	@install -Dm666 xoperantrc ${DESTDIR}${PREFIX}/share/xoperant/xoperantrc
	@install -Dm666 asoundrc2 ${DESTDIR}${PREFIX}/share/xoperant/asoundrc2
	@install -Dm666 asoundrc8 ${DESTDIR}${PREFIX}/share/xoperant/asoundrc8
	@echo "Be sure to copy the rc file to your home directory"
	@echo "==> $ cp /usr/share/xoperant/xoperantrc ~/.xoperantrc"

clean:
	@rm -rf ${PROG}
