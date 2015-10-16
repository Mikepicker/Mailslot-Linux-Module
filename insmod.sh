insmod mailslot.ko
mknod /dev/mailslot c 250 0
chmod 666 /dev/mailslot
echo -n ciao > /dev/mailslot
