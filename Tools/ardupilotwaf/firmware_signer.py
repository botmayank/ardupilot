import json, time, base64, zlib

BASE_DIR = "/media/mayank/Data/3_Drone_Dev/5_Ardupilot/npnt_botmj/"

IMAGE_PATH = BASE_DIR + "ardupilot/build/CubeOrange/bin/" + "arducopter.bin"
KEY_PATH = BASE_DIR + "privatekey.pem"
APJ_FILE = "arducopter.apj"
CUBE_FLASH_SIZE = 1835008

class Signer:
    def run_signer(self):
        print(">>>>>>>>Running Ardupilot NPNT Signer...")
        print("Image path: ", IMAGE_PATH)
        print("Key: ", KEY_PATH)
        print("APJ output: ", APJ_FILE)
        img = open(IMAGE_PATH,'rb').read()

        #sign the image if key declared
        if len(KEY_PATH):
            from Crypto.Signature import DSS
            from Crypto.PublicKey import ECC
            from Crypto.Hash import SHA256
            key = ECC.import_key(open(KEY_PATH, "r").read())
            while len(img) % 4 != 0:
                img += '\0'
            digest = SHA256.new(img)
            signer = DSS.new(key, 'fips-186-3', encoding='der')
            signature = signer.sign(digest)

            print("FW Signer Signature: ", signature)
            
            img += len(signature).to_bytes(76 - len(signature), 'big')
            img += signature
            # print(len(signature), len(len(signature).to_bytes(76 - len(signature), 'big')), '...............................')

        d = {
            "board_id": 140,
            "magic": "APJFWv1",
            "description": "Firmware for a STM32H753xx board",
            "image": base64.b64encode(zlib.compress(img,9)).decode('utf-8'),
            "summary": "CubeOrange",
            "version": "0.1",
            "image_size": len(img),
            "flash_total": CUBE_FLASH_SIZE,
            "flash_free": CUBE_FLASH_SIZE - len(img),
            "git_identity": "648dee21",
            "board_revision": 0,
            "USBID": "0x2dae/0x1016"
        }

        d["build_time"] = int(time.time())
        f = open(APJ_FILE, "w")
        f.write(json.dumps(d, indent=4))
        f.close()

if __name__ == '__main__':
    s = Signer()
    s.run_signer()
