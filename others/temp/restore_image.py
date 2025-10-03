from PIL import Image
import re
import os
import sys

def extract_pic_data(header_file):
    """从C头文件中提取图像数据和尺寸"""
    with open(header_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # 提取图像尺寸
    width_match = re.search(r'#define\s+PIC_WIDTH\s+(\d+)', content)
    height_match = re.search(r'#define\s+PIC_HEIGHT\s+(\d+)', content)
    
    if not width_match or not height_match:
        raise ValueError("在头文件中找不到图像尺寸定义")
    
    width = int(width_match.group(1))
    height = int(height_match.group(1))
    
    # 提取像素数据
    data_match = re.search(r'static\s+const\s+unsigned\s+char\s+pic_data\[\]\s+=\s+\{([\s\S]*?)\};', content)
    if not data_match:
        raise ValueError("在头文件中找不到像素数据数组")
    
    data_str = data_match.group(1)
    # 移除注释
    data_str = re.sub(r'//.*?$', '', data_str, flags=re.MULTILINE)
    # 提取所有十六进制数
    hex_values = re.findall(r'0x[0-9A-Fa-f]{2}', data_str)
    
    # 转换为整数列表
    pixel_values = [int(hex_val, 16) for hex_val in hex_values]
    
    return width, height, pixel_values

def c_array_to_image(header_file, output_path):
    """将C语言数组数据转换回图像"""
    try:
        # 提取数据
        width, height, pixel_values = extract_pic_data(header_file)
        
        # 确认数据长度与图像尺寸匹配
        if len(pixel_values) != width * height * 4:  # RGBA 每个像素4个字节
            print(f"警告: 像素数据长度 ({len(pixel_values)}) 与预期的图像尺寸不匹配 ({width}x{height}x4)")
        
        # 创建像素数据列表
        pixels = []
        for i in range(0, len(pixel_values), 4):
            if i+3 < len(pixel_values):  # 确保有足够的数据
                r = pixel_values[i]
                g = pixel_values[i+1]
                b = pixel_values[i+2]
                a = pixel_values[i+3]
                pixels.append((r, g, b, a))
        
        # 创建图像
        img = Image.new('RGBA', (width, height))
        img.putdata(pixels)
        
        # 保存图像
        img.save(output_path)
        
        print(f"成功将C数组还原为图像，保存至 {output_path}")
        print(f"图像尺寸: {width}x{height}")
        return True
        
    except Exception as e:
        print(f"错误: {e}")
        return False

if __name__ == "__main__":
    # 获取传入的参数
    if len(sys.argv) < 3:
        print("用法: python restore_image.py <C头文件路径> <输出图像路径>")
        sys.exit(1)
    
    header_file = sys.argv[1]
    output_path = sys.argv[2]
    
    # 转换C数组回图像
    success = c_array_to_image(header_file, output_path)
    
    # 退出状态码
    sys.exit(0 if success else 1)
