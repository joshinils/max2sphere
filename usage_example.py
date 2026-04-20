#!/usr/bin/env python3
"""Example usage of `calc_spherical` from `max2sphere.py`."""

# import ctypes
import datetime
import inspect
import sys

import numpy as np
import numpy.typing as npt
from PIL import Image

from max2sphere import calc_spherical


def eprint(*args, **kwargs) -> None:  # type: ignore  # pyright: ignore[reportMissingParameterType, reportUnknownParameterType]
    """Print to stderr with a timestamp and caller info for easier debugging."""
    print(f"{datetime.datetime.now()}: {inspect.stack()[1][1]}:{inspect.stack()[1][2]};{inspect.stack()[1][3]}", *args, file=sys.stderr, **kwargs)  # pyright: ignore[reportUnknownArgumentType]


def load_image(path: str) -> npt.NDArray[np.uint8]:
    """Load an image from disk and return a NumPy array of shape (height, width, 4) with dtype uint8."""

    img = Image.open(path).convert("RGBA")
    print(f"loading: {type(img) =}")
    print(f"loading: {img.size  =}")
    print(f"loading: {img.width =}")
    print(f"loading: {img.height=}")
    return np.array(img, dtype=np.uint8)


def save_image(
    image_array: npt.NDArray[np.uint8],
    path: str,
    # shape: tuple[int, int] | None = None
) -> None:
    """Save an image represented either as a NumPy array or a ctypes BITMAP4 array.

    If `image_array` is a ctypes array (returned by `calc_spherical`) the caller
    should provide `shape` as (height, width) so the flat byte buffer can be
    reshaped to (height, width, 4).
    """

    # eprint(type(image_array))
    #
    # # If this is a ctypes array of BITMAP4, convert its memory to a uint8 numpy array
    # if hasattr(image_array, "_length_") and hasattr(image_array, "_type_"):
    #     eprint("has _length_ and _type_")
    #     print(f"{image_array._length_=}")
    #     length = int(image_array._length_)
    #
    #     print(f"{image_array._type_=}")
    #     item_size = ctypes.sizeof(image_array._type_)
    #
    #     try:
    #         base = ctypes.addressof(image_array)
    #     except Exception:
    #         base = ctypes.addressof(image_array[0])
    #
    #     buf = ctypes.string_at(base, length * item_size)
    #     arr = np.frombuffer(buf, dtype=np.uint8)
    #
    #     if shape is None:
    #         # Fallback: try to infer a square-ish shape
    #         count = length
    #         h = int(np.sqrt(count))
    #         w = int(count // h)
    #     else:
    #         h, w = shape
    #
    #     image_array = arr.reshape((h, w, 4)).copy()
    # else:
    #     eprint(f"either _length_ and/or _type_ missing, assuming it's already a numpy array with {image_array.shape=} and {image_array.dtype=}")
    #     image_array = np.array(image_array, dtype=np.uint8)

    image_array = np.array(image_array, dtype=np.uint8)

    # Drop alpha channel and save as JPEG (RGB) per user preference
    if image_array.shape[-1] == 4:
        image_array = image_array[:, :, :3]

    img = Image.fromarray(image_array, mode="RGB")

    print(f"saving: {type(img) =}")
    print(f"saving: {img.size  =}")
    print(f"saving: {img.width =}")
    print(f"saving: {img.height=}")

    # subsampling = 0: equivalent to 4:4:4
    # subsampling = 1: equivalent to 4:2:2
    # subsampling = 2: equivalent to 4:2:0
    img.save(path, quality=100, subsampling=0)


def main() -> None:
    """Example usage of `calc_spherical`."""
    image1 = load_image("testframes/testframe1.jpg")
    image2 = load_image("testframes/testframe2.jpg")

    h, w = image1.shape[:2]
    # pass explicit count so C code operates on pixel count (h*w)
    result_image = calc_spherical(image1, image2, count=h * w)
    save_image(
        result_image,
        "testframes/spherical_result.jpg",
        # shape=(h, w)
    )


if __name__ == "__main__":
    main()
