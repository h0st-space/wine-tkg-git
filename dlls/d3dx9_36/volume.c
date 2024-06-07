/*
 * Copyright 2010 Christian Costa
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */


#include "d3dx9_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);

HRESULT WINAPI D3DXLoadVolumeFromFileA(IDirect3DVolume9 *dst_volume, const PALETTEENTRY *dst_palette,
        const D3DBOX *dst_box, const char *filename, const D3DBOX *src_box, DWORD filter,
        D3DCOLOR color_key, D3DXIMAGE_INFO *info)
{
    HRESULT hr;
    int length;
    WCHAR *filenameW;

    TRACE("dst_volume %p, dst_palette %p, dst_box %p, filename %s, src_box %p, filter %#lx, "
            "color_key 0x%08lx, info %p.\n",
            dst_volume, dst_palette, dst_box, debugstr_a(filename), src_box, filter, color_key, info);

    if (!dst_volume || !filename) return D3DERR_INVALIDCALL;

    length = MultiByteToWideChar(CP_ACP, 0, filename, -1, NULL, 0);
    filenameW = malloc(length * sizeof(*filenameW));
    if (!filenameW) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, filename, -1, filenameW, length);

    hr = D3DXLoadVolumeFromFileW(dst_volume, dst_palette, dst_box, filenameW,
            src_box, filter, color_key, info);
    free(filenameW);

    return hr;
}

HRESULT WINAPI D3DXLoadVolumeFromFileW(IDirect3DVolume9 *dst_volume, const PALETTEENTRY *dst_palette,
        const D3DBOX *dst_box, const WCHAR *filename, const D3DBOX *src_box, DWORD filter,
        D3DCOLOR color_key, D3DXIMAGE_INFO *info)
{
    DWORD data_size;
    HRESULT hr;
    void *data;

    TRACE("dst_volume %p, dst_palette %p, dst_box %p, filename %s, src_box %p, filter %#lx, "
            "color_key 0x%08lx, info %p.\n",
            dst_volume, dst_palette, dst_box, debugstr_w(filename), src_box, filter, color_key, info);

    if (!dst_volume || !filename) return D3DERR_INVALIDCALL;

    if (FAILED(map_view_of_file(filename, &data, &data_size)))
        return D3DXERR_INVALIDDATA;

    hr = D3DXLoadVolumeFromFileInMemory(dst_volume, dst_palette, dst_box,
            data, data_size, src_box, filter, color_key, info);
    UnmapViewOfFile(data);

    return hr;
}

static void set_d3dbox(D3DBOX *box, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint32_t front,
        uint32_t back)
{
    box->Left = left;
    box->Top = top;
    box->Right = right;
    box->Bottom = bottom;
    box->Front = front;
    box->Back = back;
}

HRESULT WINAPI D3DXLoadVolumeFromMemory(IDirect3DVolume9 *dst_volume,
        const PALETTEENTRY *dst_palette, const D3DBOX *dst_box, const void *src_memory,
        D3DFORMAT src_format, UINT src_row_pitch, UINT src_slice_pitch,
        const PALETTEENTRY *src_palette, const D3DBOX *src_box, DWORD filter, D3DCOLOR color_key)
{
    const struct pixel_format_desc *src_format_desc, *dst_format_desc;
    struct d3dx_pixels src_pixels, dst_pixels;
    RECT dst_rect_aligned, dst_rect_unaligned;
    D3DBOX dst_box_aligned, dst_box_tmp;
    D3DLOCKED_BOX locked_box;
    D3DVOLUME_DESC desc;
    HRESULT hr;

    TRACE("dst_volume %p, dst_palette %p, dst_box %p, src_memory %p, src_format %#x, "
            "src_row_pitch %u, src_slice_pitch %u, src_palette %p, src_box %p, filter %#lx, color_key 0x%08lx.\n",
            dst_volume, dst_palette, dst_box, src_memory, src_format, src_row_pitch, src_slice_pitch,
            src_palette, src_box, filter, color_key);

    if (!dst_volume || !src_memory || !src_box) return D3DERR_INVALIDCALL;

    if (src_format == D3DFMT_UNKNOWN
            || src_box->Left >= src_box->Right
            || src_box->Top >= src_box->Bottom
            || src_box->Front >= src_box->Back)
        return E_FAIL;

    if (filter == D3DX_DEFAULT)
        filter = D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER;

    src_format_desc = get_format_info(src_format);
    if (src_format_desc->type == FORMAT_UNKNOWN)
        return E_NOTIMPL;

    IDirect3DVolume9_GetDesc(dst_volume, &desc);
    dst_format_desc = get_format_info(desc.Format);
    if (dst_format_desc->type == FORMAT_UNKNOWN)
        return E_NOTIMPL;

    if (!dst_box)
    {
        set_d3dbox(&dst_box_tmp, 0, 0, desc.Width, desc.Height, 0, desc.Depth);
        dst_box = &dst_box_tmp;
    }
    else
    {
        if (dst_box->Left >= dst_box->Right || dst_box->Right > desc.Width)
            return D3DERR_INVALIDCALL;
        if (dst_box->Top >= dst_box->Bottom || dst_box->Bottom > desc.Height)
            return D3DERR_INVALIDCALL;
        if (dst_box->Front >= dst_box->Back || dst_box->Back > desc.Depth)
            return D3DERR_INVALIDCALL;
    }

    hr = d3dx_pixels_init(src_memory, src_row_pitch, src_slice_pitch,
        src_palette, src_format, src_box->Left, src_box->Top, src_box->Right, src_box->Bottom,
        src_box->Front, src_box->Back, &src_pixels);
    if (FAILED(hr))
        return hr;

    get_aligned_rect(dst_box->Left, dst_box->Top, dst_box->Right, dst_box->Bottom, desc.Width, desc.Height,
        dst_format_desc, &dst_rect_aligned);
    set_d3dbox(&dst_box_aligned, dst_rect_aligned.left, dst_rect_aligned.top, dst_rect_aligned.right,
            dst_rect_aligned.bottom, dst_box->Front, dst_box->Back);

    hr = IDirect3DVolume9_LockBox(dst_volume, &locked_box, &dst_box_aligned, 0);
    if (FAILED(hr))
        return hr;

    SetRect(&dst_rect_unaligned, dst_box->Left, dst_box->Top, dst_box->Right, dst_box->Bottom);
    OffsetRect(&dst_rect_unaligned, -dst_rect_aligned.left, -dst_rect_aligned.top);
    set_d3dx_pixels(&dst_pixels, locked_box.pBits, locked_box.RowPitch, locked_box.SlicePitch, dst_palette,
            (dst_box_aligned.Right - dst_box_aligned.Left), (dst_box_aligned.Bottom - dst_box_aligned.Top),
            (dst_box_aligned.Back - dst_box_aligned.Front), &dst_rect_unaligned);

    hr = d3dx_load_pixels_from_pixels(&dst_pixels, dst_format_desc, &src_pixels, src_format_desc, filter,
            color_key);
    IDirect3DVolume9_UnlockBox(dst_volume);
    return hr;
}

HRESULT WINAPI D3DXLoadVolumeFromFileInMemory(IDirect3DVolume9 *dst_volume, const PALETTEENTRY *dst_palette,
        const D3DBOX *dst_box, const void *src_data, UINT src_data_size, const D3DBOX *src_box,
        DWORD filter, D3DCOLOR color_key, D3DXIMAGE_INFO *src_info)
{
    HRESULT hr;
    D3DBOX box;
    D3DXIMAGE_INFO image_info;

    TRACE("dst_volume %p, dst_palette %p, dst_box %p, src_data %p, src_data_size %u, src_box %p, "
            "filter %#lx, color_key 0x%08lx, src_info %p.\n",
            dst_volume, dst_palette, dst_box, src_data, src_data_size, src_box,
            filter, color_key, src_info);

    if (!dst_volume || !src_data)
        return D3DERR_INVALIDCALL;

    if (FAILED(hr = D3DXGetImageInfoFromFileInMemory(src_data, src_data_size, &image_info)))
        return hr;

    if (src_box)
    {
        if (src_box->Right > image_info.Width
                || src_box->Bottom > image_info.Height
                || src_box->Back > image_info.Depth)
            return D3DERR_INVALIDCALL;

        box = *src_box;
    }
    else
    {
        box.Left = 0;
        box.Top = 0;
        box.Right = image_info.Width;
        box.Bottom = image_info.Height;
        box.Front = 0;
        box.Back = image_info.Depth;

    }

    if (image_info.ImageFileFormat != D3DXIFF_DDS)
    {
        FIXME("File format %#x is not supported yet\n", image_info.ImageFileFormat);
        return E_NOTIMPL;
    }

    hr = load_volume_from_dds(dst_volume, dst_palette, dst_box, src_data, &box,
            filter, color_key, &image_info);
    if (FAILED(hr)) return hr;

    if (src_info)
        *src_info = image_info;

    return D3D_OK;
}

HRESULT WINAPI D3DXLoadVolumeFromVolume(IDirect3DVolume9 *dst_volume, const PALETTEENTRY *dst_palette,
        const D3DBOX *dst_box, IDirect3DVolume9 *src_volume, const PALETTEENTRY *src_palette,
        const D3DBOX *src_box, DWORD filter, D3DCOLOR color_key)
{
    HRESULT hr;
    D3DBOX box;
    D3DVOLUME_DESC desc;
    D3DLOCKED_BOX locked_box;

    TRACE("dst_volume %p, dst_palette %p, dst_box %p, src_volume %p, src_palette %p, src_box %p, "
            "filter %#lx, color_key 0x%08lx.\n",
            dst_volume, dst_palette, dst_box, src_volume, src_palette, src_box, filter, color_key);

    if (!dst_volume || !src_volume) return D3DERR_INVALIDCALL;

    IDirect3DVolume9_GetDesc(src_volume, &desc);

    if (!src_box)
    {
        box.Left = box.Top = 0;
        box.Right = desc.Width;
        box.Bottom = desc.Height;
        box.Front = 0;
        box.Back = desc.Depth;
    }
    else box = *src_box;

    hr = IDirect3DVolume9_LockBox(src_volume, &locked_box, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) return hr;

    hr = D3DXLoadVolumeFromMemory(dst_volume, dst_palette, dst_box,
            locked_box.pBits, desc.Format, locked_box.RowPitch, locked_box.SlicePitch,
            src_palette, &box, filter, color_key);

    IDirect3DVolume9_UnlockBox(src_volume);
    return hr;
}
