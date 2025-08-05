import logging
import logging.handlers
import os

def setup_logging():
    """配置TG项目的日志记录"""
    # 获取tg目录路径并设置日志文件
    tg_dir = os.path.dirname(os.path.abspath(__file__))
    log_file = os.path.join(tg_dir, 'tg_bot.log')
    
    # 配置根日志记录器
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    
    # 避免重复添加处理器
    if logger.handlers:
        return
    
    # 创建格式化器
    formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    # 文件处理器 - 使用RotatingFileHandler避免日志文件过大
    file_handler = logging.handlers.RotatingFileHandler(
        log_file, 
        maxBytes=10*1024*1024,  # 10MB
        backupCount=5,
        encoding='utf-8'
    )
    file_handler.setLevel(logging.INFO)
    file_handler.setFormatter(formatter)
    
    # 控制台处理器
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    console_handler.setFormatter(formatter)
    
    # 添加处理器到根日志记录器
    logger.addHandler(file_handler)
    logger.addHandler(console_handler)
    
    # 设置第三方库的日志级别
    logging.getLogger('paho.mqtt').setLevel(logging.WARNING)
    logging.getLogger('urllib3').setLevel(logging.WARNING)
    logging.getLogger('requests').setLevel(logging.WARNING)
    
    logging.info("TG Bot日志系统初始化完成")
