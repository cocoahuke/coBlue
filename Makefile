CC=gcc
CFLAGS=-std=c99 -lpthread -w

checksudo = Require\ sudo\ for\ make

build/coBlued:$(checksudo) build/%.o
	$(CC) $(CFLAGS) -o $@ src/*.c src/*.h build/*.o

build/%.o:
	@echo "Compiling %.o"\
	$(shell rm -rf build;mkdir -p build;cd build; \
  cfiles=`ls ../src/ble/att/* ../src/ble/gatt/* ../src/ble/lib/* ../src/ble/util/* |grep .c$$ `;\
  files_n=`echo $$cfiles|grep -o " "|wc -l`; \
  files_n=`expr $$files_n + 1`; \
  for c in `seq 1 $$files_n`; \
  do \
      echo $$cfiles|cut -d ' ' -f $$c|xargs \
      $(CC) -isystem -w -c ; \
  done \
  ) \

$(checksudo):
	@[ -w / ] || exit 1

.PHONY:install
install:$(checksudo)
	mv build/coBlued /usr/sbin/coBlued
	chown root:root /usr/sbin/coBlued
	chmod 755 /usr/sbin/coBlued
	mv rc_coBlued /etc/init.d/coBlued
	chown root:root /etc/init.d/coBlued
	chmod 755 /etc/init.d/coBlued
	update-rc.d coBlued defaults

.PHONY:uninstall
uninstall:$(checksudo)
	update-rc.d coBlued remove
	rm -f build/coBlued
	rm -f /usr/sbin/coBlued
	rm -f /etc/init.d/coBlued

.PHONY:clean
clean:
	rm -rf build
