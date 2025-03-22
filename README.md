# simple-gtk-drawer
Simple drawing application in C made using gtk4

Compile using 

```sh
gcc -o2 -Wall -g $(pkg-config --cflags gtk4) $(pkg-config --cflags cairo) -o main main.c $(pkg-config --libs gtk4) $(pkg-config --libs cairo) -lm && ./main
```

## preview
![image](https://github.com/user-attachments/assets/a1896044-129a-4aa8-9fff-3db65e745006)
