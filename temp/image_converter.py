from PIL import Image
import os
import sys

def image_to_c_array(image_path, output_path):
    try:
        # 打开图片
        img = Image.open(image_path)
        
        # 确保图片是RGBA格式（如果不是，则转换为RGBA）
        if img.mode != 'RGBA':
            img = img.convert('RGBA')
        
        # 获取图片尺寸
        width, height = img.size
        
        # 获取像素数据
        pixels = list(img.getdata())
        
        # 创建C语言数组
        with open(output_path, 'w') as f:
            f.write('#ifndef PIC_H\n')
            f.write('#define PIC_H\n\n')
            
            # 添加图片尺寸定义
            f.write('#define PIC_WIDTH %d\n' % width)
            f.write('#define PIC_HEIGHT %d\n\n' % height)
            
            # 添加数组声明
            f.write('// RGBA格式的图片数据，透明度A均为255\n')
            f.write('static const unsigned char pic_data[] = {\n    ')
            
            # 写入像素数据，每个像素包含R、G、B、A四个字节
            for i, pixel in enumerate(pixels):
                r, g, b, _ = pixel  # 忽略原始透明度，使用255
                f.write('0x%02X, 0x%02X, 0x%02X, 0xFF' % (r, g, b))
                
                # 每8个像素换行，提高可读性
                if i < len(pixels) - 1:
                    f.write(', ')
                    if (i + 1) % 4 == 0:
                        f.write('\n    ')
            
            f.write('\n};\n\n')
            
            # 结束头文件
            f.write('#endif // PIC_H\n')
        
        print(f"成功生成C语言数组到 {output_path}")
        print(f"图片尺寸: {width}x{height}")
        return True
        
    except Exception as e:
        print(f"错误: {e}")
        return False

if __name__ == "__main__":
    # 获取传入的参数
    if len(sys.argv) < 3:
        print("用法: python image_converter.py <图片路径> <输出C头文件路径>")
        sys.exit(1)
    
    image_path = sys.argv[1]
    output_path = sys.argv[2]
    
    # 转换图片为C数组
    success = image_to_c_array(image_path, output_path)
    
    # 退出状态码
    sys.exit(0 if success else 1)
