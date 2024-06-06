import os
import shutil
import sqlite3
from datetime import datetime, timedelta
import getpass
import argparse
import subprocess

VERSION = "0.3"

BLUE = "\033[94m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
ENDC = "\033[0m"


def display_banner():
    banner = (
        r"""
___________     __         .__ __________                     .__  .__   
\__    ___/____/  |______  |  |\______   \ ____   ____ _____  |  | |  |  
  |    | /  _ \   __\__  \ |  | |       _// __ \_/ ___\\__  \ |  | |  |  
  |    |(  <_> )  |  / __ \|  |_|    |   \  ___/\  \___ / __ \|  |_|  |__
  |____| \____/|__| (____  /____/____|_  /\___  >\___  >____  /____/____/
                         \/            \/     \/     \/           
v"""
        + VERSION
        + """ / Alexander Hagenah / @xaitax / ah@primepage.de
"""
    )
    print(BLUE + banner + ENDC)


def modify_permissions(path):
    try:
        subprocess.run(
            ["icacls", path, "/grant", f"{getpass.getuser()}:(OI)(CI)F", "/T", "/C", "/Q"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        print(
            f"{GREEN}✅ Permissions modified for {path} and all its subdirectories and files{ENDC}"
        )
    except subprocess.CalledProcessError as e:
        print(f"{RED}❌ Failed to modify permissions for {path}: {e}{ENDC}")


def main(from_date=None, to_date=None, search_term=None):
    display_banner()
    username = getpass.getuser()

    base_path = f"C:\\Users\\{username}\\AppData\\Local\\CoreAIPlatform.00\\UKP"

    guid_folder = None
    if os.path.exists(base_path):
        modify_permissions(base_path)
        for folder_name in os.listdir(base_path):
            folder_path = os.path.join(base_path, folder_name)
            if os.path.isdir(folder_path):
                guid_folder = folder_path
                break
    else:
        print("🚫 Base path does not exist.")
        return

    if not guid_folder:
        print("🚫 Could not find the GUID folder.")
        return

    print(f"📁 Recall folder found: {guid_folder}")

    db_path = os.path.join(guid_folder, "ukg.db")
    image_store_path = os.path.join(guid_folder, "ImageStore")

    if not os.path.exists(db_path) or not os.path.exists(image_store_path):
        print("🚫 Windows Recall feature not found. Nothing to extract.")
        return

    proceed = input(
        "🟢 Windows Recall feature found. Do you want to proceed with the extraction? (yes/no): "
    )
    if proceed.lower() != "yes":
        print("⚠️ Extraction aborted.")
        return

    timestamp = datetime.now().strftime("%Y-%m-%d-%H-%M")
    extraction_folder = os.path.join(os.getcwd(), f"{timestamp}_Recall_Extraction")

    if not os.path.exists(extraction_folder):
        os.makedirs(extraction_folder)
        print(f"📂 Creating extraction folder: {extraction_folder}\n")
    else:
        print(f"📂 Using existing extraction folder: {extraction_folder}\n")

    shutil.copy(db_path, extraction_folder)
    shutil.copytree(
        image_store_path,
        os.path.join(extraction_folder, "ImageStore"),
        dirs_exist_ok=True,
    )

    for image_file in os.listdir(os.path.join(extraction_folder, "ImageStore")):
        image_path = os.path.join(extraction_folder, "ImageStore", image_file)
        new_image_path = f"{image_path}.jpg"
        if not new_image_path.endswith(".jpg"):
            os.rename(image_path, new_image_path)

    db_extraction_path = os.path.join(extraction_folder, "ukg.db")
    conn = sqlite3.connect(db_extraction_path)
    cursor = conn.cursor()

    from_date_timestamp = None
    to_date_timestamp = None

    if from_date:
        from_date_timestamp = (
            int(datetime.strptime(from_date, "%Y-%m-%d").timestamp()) * 1000
        )
    if to_date:
        to_date_timestamp = (
            int(
                (datetime.strptime(to_date, "%Y-%m-%d") + timedelta(days=1)).timestamp()
            )
            * 1000
        )

    query = """
    SELECT WindowTitle, TimeStamp, ImageToken 
    FROM WindowCapture 
    WHERE (WindowTitle IS NOT NULL OR ImageToken IS NOT NULL)
    """
    cursor.execute(query)
    rows = cursor.fetchall()

    output = []
    captured_windows_count = 0
    images_taken_count = 0

    captured_windows = []
    images_taken = []

    for row in rows:
        window_title, timestamp, image_token = row
        if (from_date_timestamp is None or from_date_timestamp <= timestamp) and (
            to_date_timestamp is None or timestamp < to_date_timestamp
        ):
            readable_timestamp = datetime.fromtimestamp(timestamp / 1000).strftime(
                "%Y-%m-%d %H:%M:%S"
            )
            if window_title:
                captured_windows.append(f"[{readable_timestamp}] {window_title}")
                captured_windows_count += 1
            if image_token:
                images_taken.append(f"[{readable_timestamp}] {image_token}")
                images_taken_count += 1

    output.append(f"🪟 Captured Windows: {captured_windows_count}")
    output.append(f"📸 Images Taken: {images_taken_count}")

    if search_term:
        search_query = f"""
        SELECT c1, c2 
        FROM WindowCaptureTextIndex_content 
        WHERE c1 LIKE '%{search_term}%' OR c2 LIKE '%{search_term}%'
        """
        cursor.execute(search_query)
        search_results = cursor.fetchall()
        search_results_count = len(search_results)
        output.append(f"🔍 Search results for '{search_term}': {search_results_count}")

        search_output = []
        for result in search_results:
            search_output.append(f"c1: {result[0]}, c2: {result[1]}")

    with open(
        os.path.join(extraction_folder, "TotalRecall.txt"), "w", encoding="utf-8"
    ) as file:
        file.write("Captured Windows:\n")
        file.write("\n".join(captured_windows))
        file.write("\n\nImages Taken:\n")
        file.write("\n".join(images_taken))
        if search_term:
            file.write("\n\nSearch Results:\n")
            file.write("\n".join(search_output))

    conn.close()

    for line in output:
        print(line)

    print(f"\n📄 Summary of the extraction is available in the file:")
    print(f"{YELLOW}{os.path.join(extraction_folder, 'TotalRecall.txt')}{ENDC}")
    print(f"\n📂 Full extraction folder path:")
    print(f"{YELLOW}{extraction_folder}{ENDC}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Extract and display Windows Recall data."
    )
    parser.add_argument(
        "--from_date",
        help="The start date in YYYY-MM-DD format.",
        type=str,
        default=None,
    )
    parser.add_argument(
        "--to_date", help="The end date in YYYY-MM-DD format.", type=str, default=None
    )
    parser.add_argument(
        "--search",
        help="Search term for text recognition data.",
        type=str,
        default=None,
    )
    args = parser.parse_args()

    from_date = args.from_date
    to_date = args.to_date
    search_term = args.search
    date_format = "%Y-%m-%d"
    try:
        if from_date:
            datetime.strptime(from_date, date_format)
        if to_date:
            datetime.strptime(to_date, date_format)
    except ValueError:
        parser.error("Date format must be YYYY-MM-DD.")

    main(from_date, to_date, search_term)
