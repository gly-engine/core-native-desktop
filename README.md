# Core Native Desktop

## Building

```
cmake -Bbuild -H.
```

```
make -C build
```

## Profiling

```
cmake -Bbuild -H. -DGECND_USE_PROFILE=ON
``` 

```
make -C build
```

```
export CPUPROFILE=./app.prof
```

```
export CPUPROFILE_FREQUENCY=100
```
