"""Telegram机器人主文件（重构版）

使用模块化设计，将各功能分离到独立模块。
"""

import telebot
import logging
from logging_config import setup_logging
from config import load_config
from service import BotService
from mqtt_client import MQTTClientManager
from decorators import create_permission_decorator
from callbacks import TaskCallbackHandler, AliasCallbackHandler


# 初始化日志
setup_logging()
logger = logging.getLogger(__name__)


# 加载配置
config_manager = load_config()

# 初始化Bot
bot = telebot.TeleBot(config_manager.env.ir_bot_token)

# 初始化MQTT客户端管理器
mqtt_manager = MQTTClientManager(config_manager.devices, bot)

# 初始化服务
service = BotService(bot, config_manager)


def current_mqtt_publish(data):
    """发布MQTT消息到当前设备"""
    current_device = config_manager.get_current_device()
    mqtt_manager.publish_to_device(current_device.name, data)


# 创建权限装饰器
permission = create_permission_decorator(
    bot,
    config_manager.db.to_dict(),  # 需要传递db字典以兼容原装饰器
    current_mqtt_publish
)

# 初始化回调处理器
task_callback_handler = TaskCallbackHandler(
    bot, config_manager, service, current_mqtt_publish
)
alias_callback_handler = AliasCallbackHandler(
    bot, config_manager, service, current_mqtt_publish
)


# ============= 命令处理器 =============

@bot.message_handler(commands=['copy'])
@permission.require_auth
def bot_copy(message):
    """处理copy命令"""
    return service.copy(message)


@bot.message_handler(commands=['exec'])
@permission.require_auth
def bot_exec(message):
    """处理exec命令"""
    return service.exec(message)


@bot.message_handler(commands=['terminate'])
@permission.require_auth
def bot_terminate(message):
    """处理terminate命令"""
    return service.terminate(message)


@bot.message_handler(commands=['terminatename'])
@permission.require_auth
def bot_terminatename(message):
    """处理terminatename命令"""
    return service.terminatename(message)


@bot.message_handler(commands=['cmdlist'])
@permission.require_auth
def bot_cmdlist(message):
    """处理cmdlist命令"""
    return service.cmdlist(message)


@bot.message_handler(commands=['taskidlist'])
@permission.require_auth
def bot_taskidlist(message):
    """处理taskidlist命令"""
    return service.taskidlist(message)


@bot.message_handler(commands=['tasklist'])
@permission.require_auth
def bot_tasklist(message):
    """处理tasklist命令"""
    return service.tasklist(message)


@bot.message_handler(commands=['task'])
@permission.require_auth
def bot_task(message):
    """处理task命令"""
    return service.task(message)


@bot.message_handler(commands=['device'])
@permission.require_auth
def bot_device(message):
    """处理device命令"""
    service.device(message)


@bot.message_handler(commands=['usermod'])
@permission.require_auth
def bot_usermod(message):
    """处理usermod命令"""
    service.usermod(message)


@bot.message_handler(commands=['preference'])
@permission.require_auth
def bot_preference(message):
    """处理preference命令"""
    logger.info(f"执行preference命令: user_id={message.from_user.id}")
    
    lines = [i.strip() for i in message.text.split('\n') if i and i.strip()]
    preferences = config_manager.get_current_preferences()
    
    # 添加删除别名
    adding = ""
    for line in lines[1:]:
        if line[0] == "+":  # 添加新别名
            adding = line[1:]
            preferences[adding] = []
            continue
        if line[0] == "-":  # 删除别名
            adding = ""
            if line[1:] in preferences:
                del preferences[line[1:]]
            continue
        if adding:  # 添加命令到当前别名
            preferences[adding].append(line)
    
    config_manager.update_preferences(preferences)
    logger.info(f"preference配置更新完成，当前别名数量: {len(preferences)}")
    
    # 返回别名列表
    preference_msg = "\n".join([
        f"+{k}\n    {chr(10).join('    ' + line for line in v)}"
        for k, v in preferences.items()
    ])
    bot.reply_to(message, f"alias list:\n{preference_msg}")
    
    # 执行别名
    args = [i for i in lines[0].split(" ")[1:] if i]
    for alias in args:
        alias_callback_handler._exec_alias(alias, message)


@bot.message_handler(commands=['alias'])
@permission.require_auth
def bot_alias(message):
    """处理alias命令"""
    alias_callback_handler.show_alias_menu(message)


@bot.message_handler(commands=['auth'])
@permission.no_auth_required
def bot_auth(message):
    """处理auth命令"""
    service.auth(message)


@bot.message_handler(commands=['start'])
@permission.no_auth_required
def bot_start(message):
    """处理start命令"""
    service.start(message)


@bot.message_handler(commands=['help'])
@permission.no_auth_required
def bot_help(message):
    """处理help命令"""
    service.help(message)


# ============= 回调查询处理器 =============

@bot.callback_query_handler(func=lambda call: call.data.startswith("taskid_"))
def callback_taskid(call):
    """处理任务ID终止回调"""
    task_callback_handler.handle_taskid(call)


@bot.callback_query_handler(func=lambda call: call.data.startswith("taskname_"))
def callback_taskname(call):
    """处理任务名称终止回调"""
    task_callback_handler.handle_taskname(call)


@bot.callback_query_handler(func=lambda call: call.data.startswith("alias_exc_"))
def callback_alias_exec(call):
    """处理别名执行回调"""
    alias_callback_handler.handle_exec(call)


@bot.callback_query_handler(func=lambda call: call.data.startswith("alias_del_"))
def callback_alias_delete(call):
    """处理别名删除回调"""
    alias_callback_handler.handle_delete(call)


@bot.callback_query_handler(func=lambda call: call.data == "alias_add")
def callback_alias_add(call):
    """处理别名添加回调"""
    alias_callback_handler.handle_add(call)


@bot.callback_query_handler(func=lambda call: call.data in ["alias_del", "alias_exc", "alias_cancel"])
def callback_alias_menu(call):
    """处理别名菜单切换回调"""
    alias_callback_handler.handle_menu_switch(call)


# ============= 主函数 =============

if __name__ == "__main__":
    logger.info("TG Bot启动")
    logger.info(f"当前设备: {config_manager.get_current_device().name}")
    logger.info(f"授权用户数: {len(config_manager.db.user)}")
    
    try:
        bot.infinity_polling()
    except KeyboardInterrupt:
        logger.info("收到停止信号，正在关闭...")
        mqtt_manager.disconnect_all()
    except Exception as e:
        logger.critical(f"TG Bot运行异常: {e}", exc_info=True)
        mqtt_manager.disconnect_all()
        raise
