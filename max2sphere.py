#!/usr/bin/env python3

"""
Python wrapper for the C library `libmax2sphere.so` which implements the `calc_spherical` function.
"""

import ctypes
import dataclasses
import sys

import numpy as np
import numpy.typing as npt


@dataclasses.dataclass
class BITMAP4(ctypes.Structure):
    """
    type for C struct { uint8_t r, g, b, a; }
    """

    _fields_ = [
        ("r", ctypes.c_ubyte),
        ("g", ctypes.c_ubyte),
        ("b", ctypes.c_ubyte),
        ("a", ctypes.c_ubyte),
    ]


_max2sphere = ctypes.CDLL("./libmax2sphere.so")


def init_for_calc_spherical(width: int, height: int, outwidth: int, antialias: int) -> None:
    """
    Initialize the C library for `calc_spherical` with the given parameters.
    """
    _max2sphere.init_for_calc_spherical(width, height, outwidth, antialias, sys.argv[0].encode("utf-8"))


def input_validation(frame_input1: npt.NDArray[np.uint8], frame_input2: npt.NDArray[np.uint8]) -> None:
    """
    check that inputs are valid numpy arrays of uint8 with the same shape and a known frame_template shape.
    """

    if not isinstance(frame_input1, np.ndarray) or not isinstance(frame_input2, np.ndarray):
        raise TypeError("Inputs must be NumPy arrays")
    if frame_input1.dtype.type != np.uint8 or frame_input2.dtype.type != np.uint8:
        raise TypeError("Input arrays must have dtype uint8")

    if frame_input1.shape != frame_input2.shape:
        raise ValueError("Input arrays must have the same shape")

    if frame_input1.shape not in [(1344, 4096, 4), (736, 2272, 4)]:
        raise ValueError(f"Input arrays must have a valid shape of a known frame_template, but is {frame_input1.shape=}  (e.g. (1344, 4096) or (736, 2272))")


def calc_spherical(
    frame_input1: npt.NDArray[np.uint8],
    frame_input2: npt.NDArray[np.uint8],
    count: int | None = None,
) -> npt.NDArray[np.uint8]:
    """Call the C `calc_spherical` function.

    `frame_input1` and `frame_input2` may be one of:
    - a ctypes array of `BITMAP4` (e.g. `(BITMAP4 * N)()`)
    - a NumPy array exposing `.ctypes.data_as(...)` (common pattern)
    - a ctypes pointer to `BITMAP4` (in this case you must provide `count`)

    The function allocates and returns a ctypes array of type `(BITMAP4 * count)`
    which remains valid after the call so the caller can inspect the pixels.
    """

    input_validation(frame_input1, frame_input2)

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
        except Exception as exc:
            raise TypeError("Unsupported input type for frame; provide a ctypes array, a numpy array, or a ctypes pointer") from exc

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
    out_width = int((4096 // 4) * 4)

    # out_width = 2000
    out_height = int(out_width / 2)

    init_for_calc_spherical(width=frame_input1.shape[1], height=frame_input2.shape[0], outwidth=out_width, antialias=2)

    spherical_array = np.empty((out_height, out_width, 4), dtype=np.uint8)

    spherical_ptr = _as_ptr_and_len(spherical_array)[0]
    _max2sphere.calc_spherical(ptr1, ptr2, spherical_ptr)

    return spherical_array


# If this module is imported, initialize the C library with default parameters.
if __name__ != "__main__":
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

    init_for_calc_spherical(width=4096, height=1344, outwidth=4096, antialias=2)
