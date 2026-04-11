# Using C from Python: How to create a ctypes wrapper

from: [https://pgi-jcns.fz-juelich.de/portal/pages/using-c-from-python.html](https://pgi-jcns.fz-juelich.de/portal/pages/using-c-from-python.html)

For details, please read the [ctypes documentation](http://docs.python.org/library/ctypes.html).

## Basics

This is a short tutorial on using C from Python with the help of a ctypes wrapper module. Let's say we have a small C library for calculating sums and want to use it in Python.

`sum.c`:

```C
int our_function(int num_numbers, int *numbers) {
    int i;
    int sum = 0;
    for (i = 0; i < num_numbers; i++) {
        sum += numbers[i];
    }
    return sum;
}
```

Compile it:

```shell
cc -fPIC -shared -o libsum.so sum.c
```

Now to the main part, first the complete listing and then a line by line explanation: `sum.py`:

```python
import ctypes

_sum = ctypes.CDLL('libsum.so')
_sum.our_function.argtypes = (ctypes.c_int, ctypes.POINTER(ctypes.c_int))

def our_function(numbers):
    global _sum
    num_numbers = len(numbers)
    array_type = ctypes.c_int * num_numbers
    result = _sum.our_function(ctypes.c_int(num_numbers), array_type(*numbers))
    return int(result)
```

We import the ctypes module in the first line, then the shared library is loaded (usually you would want to include a check for the operating system here and name a dll instead, if running on windows). It is stored in the variable `_sum` and the functions in this library can be accessed as members (e.g. `_sum.our_functions`). In the next line, we set the argument types for the function. The function `ctypes.POINTER` creates a pointer datatype, so `ctypes.POINTER(ctypes.c_int)` is the ctypes datatype for `int *`. When using the functions, ctypes will check if the arguments fit to these types.

The real function definition follows in the next lines. We don't need the num_numbers argument as the length of Python sequences can be queried. This is the first thing we do in the function (after quickly stating that `_sum` is global to help the interpreter find it). Next, we need to define a ctypes array type, which is binary compatible to what the library expects. This can be done by 'multiplying' the type `ctypes.c_int` with an integer. To create an array with this type, we use:

```python
array_type(*numbers)
```

The type expects each array element as an argument, so we use the asterisk notation with `numbers`. The result has the type `ctypes.c_int` and we want to return a python integer, therefore `int(result)` is returned.

Using this wrapper module is quite straight-forward:

``` python
import sum
print sum.our_function([1,2,-3,4,-5,6])
```

## Callback Functions

`ctypes` uses a library that creates functions which follow the platform-dependent calling convention during runtime. Thanks to that, it is also possible to wrap a Python function in a way that it becomes callable from C. When dealing with APIs that include events (e.g. a GUI library), this comes in very helpful, as it allows us to use Python functions as C callbacks.

To do this, we need to create a function type with the method `ctypes.CFUNCTYPE`:

``` python
callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_float, ctypes.c_float)
```

This creates a function type which is equivalent to the following C type:

``` C
typedef int (*callback_type)(float, float);
```

Next we create an instance of this type with our Python function:

``` python
def greater_than(a,b):
    if a > b:
        return 1
    else:
        return 0
callback_func = callback_type(greater_than)
```

This ```callback_func``` can be used as a normal argument to C functions now. It is very important to make sure that you keep a reference to this type for as long as any C library might call it. If it gets deleted by the garbage collector, calling it from C is likely to either cause a segfault or maybe even interpret random memory as machine language.
