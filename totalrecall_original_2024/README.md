
# TotalRecall - a 'privacy nightmare'?

This very simple tool extracts and displays data from the Recall feature in Windows 11, providing an easy way to access information about your PC's activity snapshots.

## What is Windows Recall?

On May 20th 2024 [Microsoft announced its new Copilot+ PCs](https://blogs.microsoft.com/blog/2024/05/20/introducing-copilot-pcs/) running on ARM architecture.

With this, they also announced Windows Copilot+ Recall which will be released on June 18th 2024.

![image](https://github.com/xaitax/TotalRecall/assets/5014849/eff4619c-700c-47d7-bb89-a05054309517)

> **Retrace your steps with Recall**
> Search across time to find the content you need. Then, re-engage with
> it. With Recall, you have an explorable timeline of your PC’s past.
> Just describe how you remember it and Recall will retrieve the moment
> you saw it. Any photo, link, or message can be a fresh point to
> continue from. As you use your PC, Recall takes snapshots of your
> screen. Snapshots are taken every five seconds while content on the
> screen is different from the previous snapshot. Your snapshots are
> then locally stored and locally analyzed on your PC. Recall’s analysis
> allows you to search for content, including both images and text,
> using natural language. Trying to remember the name of the Korean
> restaurant your friend Alice mentioned? Just ask Recall and it
> retrieves both text and visual matches for your search, automatically
> sorted by how closely the results match your search. Recall can even
> take you back to the exact location of the item you saw.

## Requirements

To run or use this feature, you need to have one of the new Copilot+ PCs running on ARM. Some of them can be found [here](https://www.microsoft.com/en-us/windows/copilot-plus-pcs?r=1#shop)

![image](https://github.com/xaitax/TotalRecall/assets/5014849/972aedc8-6e3c-4c5a-8d40-55b534d248b4)

### How can I play with it if it's not released yet?

Some smart folks released [AmperageKit](https://github.com/thebookisclosed/AmperageKit), which shows how you can either emulate such an ARM machine locally or spin one up on Azure. I opted for the latter.

## Technical Details

> Earlier this month, Microsoft’s CEO emailed all their staff saying
> “If you’re faced with the tradeoff between security and another
> priority, your answer is clear:  **Do security**.”

So, do they? Not quite. Windows Recall stores everything locally in an unencrypted SQLite database, and the screenshots are simply saved in a folder on your PC. Here’s where you can find them:

    C:\Users\$USER\AppData\Local\CoreAIPlatform.00\UKP\{GUID}

The images are all stored in the following subfolder

    .\ImageStore\

The database, `ukg.db`, is relatively straightforward in its structure, but it holds a wealth of information.

![image](https://github.com/xaitax/TotalRecall/assets/5014849/bb389892-d593-4c35-ba26-d8f69a6dd5f8)

## TotalRecall

![image](https://github.com/xaitax/TotalRecall/assets/5014849/e87224bf-4dd2-4049-964e-0a89d6563b99)

**So what does the tool do?**

TotalRecall copies the databases and screenshots and then parses the database for potentially interesting artifacts. You can define dates to limit the extraction as well as search for strings (that were extracted via Recall OCR) of interest. There is no rocket science behind all this. It's very basic SQLite parsing.

```bash
$ totalrecall.py -h
usage: totalrecall.py [-h] [--from_date FROM_DATE] [--to_date TO_DATE] [--search SEARCH]

Extract and display Windows Recall data.

options:
-h, --help            show this help message and exit
--from_date FROM_DATE The start date in YYYY-MM-DD format.
--to_date TO_DATE     The end date in YYYY-MM-DD format.
--search SEARCH       Search term for text recognition data.
```

### Example Output

```bash
$ totalrecall.py --search password --from_date 2024-06-04 --to_date 2024-06-05

___________     __         .__ __________                     .__  .__
\__    ___/____/  |______  |  |\______   \ ____   ____ _____  |  | |  |
  |    | /  _ \   __\__  \ |  | |       _// __ \_/ ___\\__  \ |  | |  |
  |    |(  <_> )  |  / __ \|  |_|    |   \  ___/\  \___ / __ \|  |_|  |__
  |____| \____/|__| (____  /____/____|_  /\___  >\___  >____  /____/____/
                         \/            \/     \/     \/
v0.3 / Alexander Hagenah / @xaitax / ah@primepage.de

✅ Permissions modified for C:\Users\alex\AppData\Local\CoreAIPlatform.00\UKP and all its subdirectories and files
📁 Recall folder found: C:\Users\alex\AppData\Local\CoreAIPlatform.00\UKP\{D87DDB65-90BE-4399-BB1B-5BEB0B1D12CB}
🟢 Windows Recall feature found. Do you want to proceed with the extraction? (yes/no): yes
📂 Creating extraction folder: C:\Users\alex\Downloads\TotalRecall\2024-06-06-21-02_Recall_Extraction

🪟 Captured Windows: 166
📸 Images Taken: 46
🔍 Search results for 'password': 32

📄 Summary of the extraction is available in the file:
C:\Users\alex\Downloads\TotalRecall\2024-06-06-21-02_Recall_Extraction\TotalRecall.txt

📂 Full extraction folder path:
C:\Users\alex\Downloads\TotalRecall\2024-06-06-21-02_Recall_Extraction
```

### How TotalRecall Works

1. **Data Extraction**:
    - TotalRecall copies the `ukg.db` database and the `ImageStore` folder to a specified extraction folder. This ensures the original data remains intact while you explore the extracted data.

2. **Database Parsing**:
    - It parses the SQLite database to extract potentially interesting artifacts, such as window titles, timestamps, and image tokens. The tool looks for entries that match the criteria you specify (e.g., date range, search terms).

3. **Screenshot Management**:
    - TotalRecall renames the image files in the `ImageStore` folder with a `.jpg` extension if they don't already have one. This makes it easier to view and manage the screenshots.

4. **Search Functionality**:
    - You can search for specific terms within the database, leveraging the Optical Character Recognition (OCR) capabilities of Windows Recall. This means you can find text that appeared on your screen, even if it was within an image.

5. **Output Generation**:
    - The tool generates a summary of the extracted data, including counts of captured windows and images taken. It also creates a detailed report in a text file, listing all the captured data and search results.

### Key Features

- **Date Filtering**:
  - Specify start and end dates to limit the extraction to a particular time frame.

- **Text Search**:
  - Search for specific text within the captured data, making it easy to find relevant information.

- **Comprehensive Reports**:
  - Generate detailed reports summarizing the captured windows, images, and search results, all stored in a `TotalRecall.txt` file for easy reference.

TotalRecall provides a straightforward way to explore the data collected by Windows Recall. It's no rocket science whatsoever.

## Changelog

### [06. June 2024] - Version 0.3

- **Permission Fix**: Added the `modify_permissions` function to ensure the script has the necessary permissions to access and manipulate files within the target directories, using the `icacls` command. Thank you [James Forshaw](https://x.com/tiraniddo).

### [04. June 2024] - Version 0.2

- **Initial release**

## FAQ (Update 08.06.2024)

Kevin Beaumont ([@GossiTheDog](https://x.com/GossiTheDog)) wrote a [very good article](https://doublepulsar.com/recall-stealing-everything-youve-ever-typed-or-viewed-on-your-own-windows-pc-is-now-possible-da3e12e9465e) about the Recall disaster as well with a spot-on FAQ - bare in mind all of this will likely change for the final release as we have [learned yesterday](https://blogs.windows.com/windowsexperience/2024/06/07/update-on-the-recall-preview-feature-for-copilot-pcs/).
