import telebot
from telebot import types
import config_manager
import bot_service
import util
import mqtt
from logging_config import setup_logging
import logging

# 在任何其他导入和代码之前初始化日志
setup_logging()
logger = logging.getLogger(__name__)

env, devices, db = config_manager.load_config()

bot = telebot.TeleBot(env["ir_bot_token"])

mqttClients = mqtt.MutiMqttClients(devices, bot)

service = bot_service.Service(bot, env, devices, db)


def current_mqtt_publish(data):
    mqttClients.get(db["device"]["name"]).publish(db["device"]["ir_pub_topic"], str(data), 0)

def Permissions(func):
    def wrapper(*args, **kwargs):
        # 只保留ascii可见字符和空格换行
        args[0].text =  ''.join(filter(lambda char: 32 <= ord(char) <= 126 or char == '\n', args[0].text))
        logger.debug(f"权限检查: {args[0]}")
        message = args[0]
        if message.chat.id not in db["user"]:
            logger.warning(f"未授权用户尝试访问: user_id={message.from_user.id}, chat_id={message.chat.id}")
            bot.reply_to(message, f"authentication required")
            return 
        logger.info(f"用户权限验证通过: user_id={message.from_user.id}, chat_id={message.chat.id}")
        data = func(*args, **kwargs)  # 调用被装饰的函数，并传递所有参数
        if data:
            logger.info(f"发送MQTT消息: topic={db['device']['ir_pub_topic']}, data={str(data)}")
            current_mqtt_publish(data)
            bot.send_message(message, f"{str(data)} is transmitted")
    return wrapper

### Bot command handlers

@bot.message_handler(commands=['preference'])
@Permissions
def bot_preference(message):
    logger.info(f"执行preference命令: user_id={message.from_user.id}")
    lines = [i.strip() for i in message.text.split('\n') if i and i.strip()]
    pref = db["preference"][db["device"]["name"]]
    # 添加删除别名
    adding = ""
    for line in lines[1:]: 
        if line[0] == "+": #不存在空行
            adding = line[1:]
            pref[adding] = []
            continue
        if line[0] == "-":
            adding = ""
            if line[1:] in pref: del pref[line[1:]]
            continue
        if adding:
            pref[adding].append(line)
    util.save_dict(util.DBFILE, db)
    logger.info(f"preference配置更新完成，当前别名数量: {len(db['preference'])}")

    # 返回别名列表
    preference_msg = "\n".join([ f"{k}\n    {"\n    ".join(v)}" for k,v in pref.items()])
    bot.reply_to(message, f"alias list:\n{preference_msg}")

    # 执行别名
    args = [i for i in lines[0].split(" ")[1:] if i]
    for alias in args:
        exec_alias(alias, message)

def exec_alias(alias, message):
    pref = db["preference"][db["device"]["name"]]
    if alias in pref:
        for seq in pref[alias]:
            cmd = [i for i in seq.split(" ") if i]
            if len(cmd) == 0: continue
            cmd = cmd[0]
            message.text = seq
            try:
                data = util.dynamic_call(service, cmd, message)
                if data:
                    logger.info(f"命令执行成功: {cmd}, data={str(data)}")
                    current_mqtt_publish(data)
                    bot.send_message(message.chat.id, f"{str(data)} is transmitted")
            except AttributeError as e:
                logger.error(f"命令不存在: {cmd}")
                bot.send_message(message.chat.id, f"command {cmd} not found")
    else:
        bot.send_message(message.chat.id, f"alias {alias} not found")

@bot.message_handler(commands=['copy'])
@Permissions
def bot_copy(message):
    return service.copy(message)
    

@bot.message_handler(commands=['exec'])
@Permissions
def bot_exec(message):
    return service.exec(message)
    
@bot.message_handler(commands=['terminate'])
@Permissions
def bot_terminate(message):
    return service.terminate(message)

@bot.message_handler(commands=['terminatename'])
@Permissions
def bot_terminatename(message):
    return service.terminatename(message)

@bot.message_handler(commands=['cmdlist'])
@Permissions
def bot_cmdlist(message):
    return service.cmdlist(message)


@bot.message_handler(commands=['taskidlist'])
@Permissions
def bot_taskidlist(message):
    return service.taskidlist(message)


@bot.message_handler(commands=['tasklist'])
@Permissions
def bot_tasklist(message):
    return service.tasklist(message)

@bot.message_handler(commands=['task'])
@Permissions
def bot_task(message):
   return service.task(message)

@bot.message_handler(commands=['device'])
@Permissions
def bot_device(message):
    service.device(message) 

@bot.message_handler(commands=['usermod'])
@Permissions
def bot_usermod(message):
    service.usermod(message)


@bot.message_handler(commands=['auth'])
def bot_request(message):
    service.auth(message)


@bot.message_handler(commands=['start'])
def bot_start(message):
    service.start(message)

@bot.message_handler(commands=['help'])
def bot_help(message):
    service.help(message)

            
@bot.message_handler(commands=['alias'])
@Permissions
def bot_alias(message):
    logger.info(f"执行alias命令: user_id={message.from_user.id}")
    go_alias_menu(message, send=True) # 发送主菜单

### Callbacks for inline buttons

@bot.callback_query_handler(func=lambda call: call.data.startswith("taskid_"))
def taskid_(call): # 任务菜单 终止任务 id
    call.message.text = f"terminate {call.data[7:]}"
    data = service.terminate(call.message)
    if data:
        current_mqtt_publish(data)
        bot.send_message(call.message.chat.id, f"{str(data)} is transmitted")
    bot.answer_callback_query(call.id)
    bot_tasklist(call.message) # 刷新任务列表

@bot.callback_query_handler(func=lambda call: call.data.startswith("taskname_"))
def taskname_(call): # 任务菜单 终止任务 name
    call.message.text = f"terminatename {call.data[9:]}"
    data = service.terminatename(call.message)
    if data:
        current_mqtt_publish(data)
        bot.send_message(call.message.chat.id, f"{str(data)} is transmitted")
    bot.answer_callback_query(call.id)
    bot_tasklist(call.message) # 刷新任务列表

@bot.callback_query_handler(func=lambda call: call.data.startswith("alias_exc_"))
def alias_exec_(call): # alias exc子菜单 执行具体别名选项
    alias = call.data[10:]
    exec_alias(alias, call.message)
    bot.answer_callback_query(call.id)
    # go_alias_exc_menu(call)

@bot.callback_query_handler(func=lambda call: call.data.startswith("alias_del_"))
def alias_del_(call): # alias del子菜单 删除具体别名选项
    alias = call.data[10:]
    pref = db["preference"][db["device"]["name"]]
    if alias not in pref:
        return
    del pref[alias]
    util.save_dict(util.DBFILE, db)
    bot.answer_callback_query(call.id)
    go_alias_del_menu(call.message) # 刷新删除菜单
    

@bot.callback_query_handler(func=lambda call: call.data == "alias_add")
def alias_add(call): # alias菜单，添加别名选项。完成后发送新alias菜单
    msg = bot.send_message(call.message.chat.id, f"first input alias name, then input commands. e.g.\nmyalias\nmycommand arg1 arg2")
    def add_command(message):
        lines = [i.strip() for i in message.text.split('\n') if i and i.strip()]
        if len(lines) < 2:
            bot.send_message(message.chat.id, f"invalid format")
            return
        alias = lines[0]
        db["preference"][db["device"]["name"]][alias] = []
        for line in lines[1:]:
            db["preference"][db["device"]["name"]][alias].append(line)
        util.save_dict(util.DBFILE, db)
        bot.answer_callback_query(call.id)
        go_alias_menu(call.message, send=True) # 发送主菜单
    bot.register_next_step_handler(msg, add_command)
    

def alias_list_msg(message, markup, text, send=False): # 发送或编辑alias列表消息
    preference_msg = "\n".join([ f"{k}\n    {"\n    ".join(v)}" for k,v in db["preference"][db["device"]["name"]].items()])
    if send:
        bot.send_message(message.chat.id, f"alias list:\n{preference_msg}\n\n{text}", reply_markup = markup)
    else:
        bot.edit_message_text(
            f"alias list:\n{preference_msg}\n\n{text}", 
            chat_id=message.chat.id, 
            message_id=message.message_id,
            reply_markup = markup
        )

def go_alias_del_menu(message, send=False):
    markup = types.InlineKeyboardMarkup()
    for k in db["preference"][db["device"]["name"]]:
        btn = types.InlineKeyboardButton(k, callback_data=f"alias_del_{k}")
        markup.add(btn)
    markup.add(types.InlineKeyboardButton("⬅ back to alias list", callback_data="alias_cancel"))

    alias_list_msg(message, markup, "which alias del", send)

def go_alias_exc_menu(message, send=False):
    markup = types.InlineKeyboardMarkup()
    for k in db["preference"][db["device"]["name"]]:
        btn = types.InlineKeyboardButton(k, callback_data=f"alias_exc_{k}")
        markup.add(btn)
    markup.add(types.InlineKeyboardButton("⬅ back to alias list", callback_data="alias_cancel"))

    alias_list_msg(message, markup, "which alias exc", send)

def go_alias_menu(message, send=False):
    markup = types.InlineKeyboardMarkup()
    buttons = [
       types.InlineKeyboardButton("exc", callback_data="alias_exc"),
       types.InlineKeyboardButton("add", callback_data="alias_add"),
       types.InlineKeyboardButton("del", callback_data="alias_del")
    ]
    markup.add(*buttons)

    alias_list_msg(message, markup, "which alias option", send)

@bot.callback_query_handler(func=lambda call: call.data == "alias_del")
def alias_del(call): # alias菜单 变换 删除子页面    
    bot.answer_callback_query(call.id)
    go_alias_del_menu(call.message)

@bot.callback_query_handler(func=lambda call: call.data == "alias_exc")
def alias_exc(call): # alias菜单 变换 执行子页面    
    bot.answer_callback_query(call.id)
    go_alias_exc_menu(call.message)
    
@bot.callback_query_handler(func=lambda call: call.data == "alias_cancel")
def alias_cancel(call): # 子页面 变换 alias主菜单 
    bot.answer_callback_query(call.id)
    go_alias_menu(call.message)


if __name__ == "__main__":
    logger.info("TG Bot启动")
    try:
        bot.infinity_polling()
    except Exception as e:
        logger.critical(f"TG Bot运行异常: {e}")
        raise



