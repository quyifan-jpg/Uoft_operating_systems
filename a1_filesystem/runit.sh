truncate -s 524288 example
./mkfs.a1fs -f -i 4 example
./a1fs example /tmp/quyifan1
mkdir /tmp/quyifan1/dir1
mkdir /tmp/quyifan1/dir2
mkdir /tmp/quyifan1/dir1/dir11
rmdir /tmp/quyifan1/dir1/dir11
 
echo "123" > /tmp/quyifan1/afile
cat /tmp/quyifan1/afile