import util

def load_config():
    env = util.load_dict(util.ENVFILE)
    devices = {i["name"] : i for i in env["device"]}
    db = util.load_dict(util.DBFILE)

    if "device" not in db:
        db["device"] = env["device"][0]

    if"user" not in db:
        db["user"] = [env["ir_admin_chat_id"]]

    if"preference" not in db:
        db["preference"] = {}

    util.save_dict("db.json", db)
    return env, devices, db