#!/usr/bin/env python3

import sys

import numpy as np
import numpy.typing as npt
import ctypes

_max2sphere = ctypes.CDLL('./libmax2sphere.so')


class BITMAP4(ctypes.Structure):
    _fields_ = [
        ("r", ctypes.c_ubyte),
        ("g", ctypes.c_ubyte),
        ("b", ctypes.c_ubyte),
        ("a", ctypes.c_ubyte),
    ]


# void calc_spherical(BITMAP4* frame_input1, BITMAP4* frame_input2, BITMAP4* spherical_out)
_max2sphere.calc_spherical.argtypes = (
    ctypes.POINTER(BITMAP4),
    ctypes.POINTER(BITMAP4),
    ctypes.POINTER(BITMAP4),
)
_max2sphere.calc_spherical.restype = None


# void init_for_calc_spherical(int width, int height, int outwidth, int antialias, const char* argv0)
_max2sphere.init_for_calc_spherical.argtypes = (
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_char_p,
)
_max2sphere.init_for_calc_spherical.restype = None

_max2sphere.init_for_calc_spherical(4096, 1344, 4096, 2, sys.argv[0].encode('utf-8'))


def calc_spherical(
    frame_input1: npt.NDArray[np.uint8],
    frame_input2: npt.NDArray[np.uint8],
    count: int | None = None
) -> npt.NDArray[np.uint8]:
    """Call the C `calc_spherical` function.

    `frame_input1` and `frame_input2` may be one of:
    - a ctypes array of `BITMAP4` (e.g. `(BITMAP4 * N)()`)
    - a NumPy array exposing `.ctypes.data_as(...)` (common pattern)
    - a ctypes pointer to `BITMAP4` (in this case you must provide `count`)

    The function allocates and returns a ctypes array of type `(BITMAP4 * count)`
    which remains valid after the call so the caller can inspect the pixels.
    """

    def _as_ptr_and_len(obj: npt.NDArray[np.uint8]) -> tuple[ctypes.POINTER(BITMAP4), int | None]:  # type: ignore
        # ctypes arrays expose `_length_`
        if hasattr(obj, "_length_"):
            return ctypes.cast(obj, ctypes.POINTER(BITMAP4)), getattr(obj, "_length_")  # type: ignore

        # numpy arrays and similar expose a `.ctypes.data_as(...)`
        if hasattr(obj, "ctypes") and hasattr(obj.ctypes, "data_as"):
            ptr = obj.ctypes.data_as(ctypes.POINTER(BITMAP4))
            length = getattr(obj, "size", None)
            return ptr, length

        # Fallback: try to cast (works if obj is already a pointer-like)
        try:
            return ctypes.cast(obj, ctypes.POINTER(BITMAP4)), None  # type: ignore
        except Exception:
            raise TypeError("Unsupported input type for frame; provide a ctypes array, a numpy array, or a ctypes pointer")

    ptr1, len1 = _as_ptr_and_len(frame_input1)
    ptr2, len2 = _as_ptr_and_len(frame_input2)

    # determine count: prefer provided `count`, else lengths reported by inputs
    if count is None:
        if len1 is not None:
            count = len1
        elif len2 is not None:
            count = len2
        else:
            raise ValueError("count is required when input objects do not expose length")

    # allocate output buffer of BITMAP4 elements
    # output size: 4096 × 2048
    spherical_array = np.empty((2048, 4096, 4), dtype=np.uint8)
    # initialize to known "random" data for testing
    spherical_array[:] = np.random.randint(0, 256, size=(2048, 4096, 4), dtype=np.uint8)

    before = spherical_array.copy()

    print(f" pre: {(before == spherical_array).all()} {before is spherical_array=}")

    spherical_ptr = _as_ptr_and_len(spherical_array)[0]
    _max2sphere.calc_spherical(ptr1, ptr2, spherical_ptr)

    print(f"post: {(before == spherical_array).all()} {before is spherical_array=}")

    return spherical_array
