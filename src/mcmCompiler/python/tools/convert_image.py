#!/usr/bin/python3
#
# Copyright (C) 2022 Intel Corporation
# SPDX-License-Identifier: Apache 2.0
#

import argparse
import numpy as np
import cv2
import ast
import re

# Changes the order of channels in an input image. Taken from original Fathom project
#
# command:
#   python3 convert_image.py --image <path to image> --shape N,C,H,W


# Apply scale, mean, and channel_swap to array
def preprocess_img(data, raw_scale=1, mean=None, channel_swap=None):
    if raw_scale is not None and raw_scale != 1:
        data *= raw_scale

    if channel_swap is not None:
        data[0] = data[0][np.argsort(channel_swap), :, :]

    if mean is not None and mean != 0:
        # Try loading mean from .npy file
        if re.search('[a-zA-Z]+', mean):
            try:
                mean = np.load(mean)
            except BaseException:
                throw_error(ErrorTable.InvalidNpyFile, mean)

            mean = mean.mean(1).mean(1)
            mean_arr = np.zeros(data.shape[1:])

            for x in range(mean.shape[0]):
                mean_arr[x].fill(mean[x])

            data[0] -= mean_arr

        # Else, try loading mean as tuple
        elif re.search('[,]+', mean):
            try:
                (R, G, B) = mean.split(',')
            except BaseException:
                throw_error(ErrorTable.InvalidTuple, mean)

            mean = np.asarray([float(R), float(G), float(B)])
            mean_arr = np.zeros(data.shape[1:])

            for x in range(mean.shape[0]):
                mean_arr[x].fill(mean[x])

            data[0] -= mean_arr

        # Else, load mean as single number
        elif re.search(r'\d+', mean):
            try:
                data = data - float(mean)
            except BaseException:
                throw_error(ErrorTable.InvalidMean, mean)

        # Else, invalid mean input
        else:
            throw_error(ErrorTable.InvalidMean, mean)
    return data

def parse_img(path, new_size, raw_scale=1, mean=None, channel_swap=None, dtype=np.uint8):
    """
    Parse an image with the Python Imaging Libary and convert to 4D numpy array

    :param path:
    :param new_size:
    :return:
    """
    import PIL
    from PIL import Image
    import skimage
    import skimage.io
    import skimage.transform

    if path.split(".")[-1].lower() in ["png", "jpeg", "jpg", "bmp", "gif"]:
        # greyscale = True if new_size[1] == 1 else False
        # if dtype in [np.uint8, np.int8]:
        #     data = skimage.img_as_ubyte(
        #         skimage.io.imread(
        #             path, as_gray=greyscale)).astype(
        #         dtype)
        # else:
        #     data = skimage.img_as_float(
        #         skimage.io.imread(
        #             path, as_gray=greyscale)).astype(
        #         np.float32)
        data = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    else:
        throw_error("Exception: Unsupported image type")

    if (len(data.shape) == 2):
        # Add axis for greyscale images (size 1)
        data = data[:, :, np.newaxis]

    # this line seems to be troublesome, stripping values from data
    # data = skimage.transform.resize(data, new_size[2:], preserve_range=True).astype(dtype)
    
    # resize image
    if data.shape[2] != new_size[2]:
        print("Resizing image from " + str(data.shape) + " to (" + str(new_size[2]) + ", " + str(new_size[3]) + ", " + str(new_size[1]) + ")")
        dim = (new_size[2], new_size[3])
        data = cv2.resize(data, dim, interpolation = cv2.INTER_AREA)
    
    # transpose to channel major format
    data = np.transpose(data, (2, 0, 1))
    data = np.reshape(data, (1, data.shape[0], data.shape[1], data.shape[2]))

    #data = preprocess_img(data, raw_scale, mean, channel_swap)

    return data


def convert_image(image, shape, dtype,
                raw_scale=1, mean=0, channel_swap=None,
                range_min=0, range_max=256):
    import PIL
    from PIL import Image
    im = Image.open(open(image, 'rb'))
    width, height = im.size

    new_shape = [int(shape[0]),
                 int(shape[1]),
                 int(shape[2]),
                 int(shape[3])]

    input_data = parse_img(image, new_shape,
                            raw_scale=raw_scale,
                            mean=mean,
                            channel_swap=channel_swap,
                            dtype=dtype)

    return input_data

def arg_as_list(s):
    v = ast.literal_eval(s)
    if type(v) is not list:
        raise argparse.ArgumentTypeError("Argument \"%s\" is not a list" % (s))
    return v


def transpose_image(tensor_data, Zmajor):
    layouts_zmajor = (0,2,3,1)
    layouts_chmajor= (0,1,2,3)

    layout=layouts_chmajor
    if Zmajor is True:
        print("selecting z major layout for image")
        layout=layouts_zmajor

    t_data = np.transpose(tensor_data, layout)
    return t_data


def main():
    parser = argparse.ArgumentParser(description='Convert the image used to a format suitable for KMB.')
    parser.add_argument('--image', type=str, required=True, help='an image to test the model')
    parser.add_argument('--shape', type=str)
    parser.add_argument('--zmajor', action='store_true')

    args = parser.parse_args()

    image_shape = args.shape.split(',')

    processed_image = convert_image(args.image, image_shape, np.uint8)
    
    # images are in NCHW format --> convert it to NCWH format. For whatever reason, this is what
    # Fathom generates as input
    # processed_image = processed_image.transpose([0, 1, 3, 2])

    transposed_image = transpose_image(processed_image, args.zmajor)
    
    fp = open("converted_image.dat", "wb")
    fp.write ((transposed_image.flatten()).astype('uint8').data)
    fp.close

if __name__ == "__main__":
    main()
