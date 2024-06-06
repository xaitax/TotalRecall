
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

## FAQ

Kevin Beaumont ([@GossiTheDog](https://x.com/GossiTheDog)) wrote a [very good article](https://doublepulsar.com/recall-stealing-everything-youve-ever-typed-or-viewed-on-your-own-windows-pc-is-now-possible-da3e12e9465e) about the Recall disaster as well with a spot-on FAQ that I will blatantly steal with his permission.

 **Q. The data is processed entirely locally on your laptop, right?**

A. Yes! They made some smart decisions here, there’s a whole subsystem of Azure AI etc code that process on the edge.

**Q. Cool, so hackers and malware can’t access it, right?**

A. No, they can.

**Q. But it’s encrypted.**

A. When you’re logged into a PC and run software, things are decrypted for you. Encryption at rest only helps if somebody comes to your house and physically steals your laptop — that isn’t what criminal hackers do.

For example, InfoStealer trojans, which automatically steal usernames and passwords, are a major problem for well over a decade — now these can just be easily modified to support Recall.

**Q. But the BBC said data cannot be accessed remotely by hackers.**

A. They were quoting Microsoft, but this is wrong. Data can be accessed remotely.

![](https://miro.medium.com/v2/resize:fit:1050/1*SVqXnyYIsARzArDwLCt7nA.png)

This is what the journalist was told for some reason:

![](https://miro.medium.com/v2/resize:fit:1050/0*eGSNbJ_KhwHAlV7p.png)

**Q. Microsoft say only that user can access the data.**

A. This isn’t true, I can demonstrate another user account on the same device accessing the database.

**Q. So how does it work?**

A. Every few seconds, screenshots are taken. These are automatically OCR’d by Azure AI, running on your device, and written into an SQLite database in the user’s folder.

This database file has a record of everything you’ve ever viewed on your PC in plain text. OCR is a process of looking an image, and extracting the letters.

**Q. What does the database look like?**

A:

**Q. How do you obtain the database files?**

A. They’re just files in AppData, in the new CoreAIPlatform folder.

**Q. But it’s highly encrypted and nobody can access them, right?!**

A. Here’s a few second video of two Microsoft engineers accessing the folder:

**Q. …But, normal users don’t run as admins!**

A. According to Microsoft’s own website, in their Recall rollout page, they do:

![](https://miro.medium.com/v2/resize:fit:1050/0*WGE1jcRzhe6WAGQS)

In fact, you don’t even need to be an admin to read the database — more on that in a later blog.

**Q. But a UAC prompt appeared in that video, that’s a security boundary.**

A. According to Microsoft’s own website (and MSRC), UAC is not a security boundary:

![](https://miro.medium.com/v2/resize:fit:1050/1*TTjYNH15IoP_d8JhhG3cEA.png)

**Q. So… where is the security here?**

A. They have tried to do a bunch of things but none of it actually works properly in the real world due to gaps you can drive a plane through.

**Q. Does it automatically not screenshot and OCR things like financial information?**

A. No:

![](https://miro.medium.com/v2/resize:fit:1050/1*OZMjujpALL3IfAQYT64x7Q.png)

**Q. How large is the database?**

A. It compresses well, several days working is around ~90kb. You can exfiltrate several months of documents and key presses in the space of a few seconds with an average broadband connection.

**Q. How fast is search?**

On device, really fast.

**Q. Have you exfiltrated your own Recall database?**

A. Yes. I have automated exfiltration, and made a website where you can upload a database and instantly search it.

I am deliberately holding back technical details until Microsoft ship the feature as I want to give them time to do something.

I actually have a whole bunch of things to show and think the wider cyber community will have so much fun with this when generally available... but I also think that’s really sad, as real world harm will ensue.

**Q. What kind of things are in the database?**

A. Everything a user has ever seen, ordered by application. Every bit of text the user has seen, with some minor exceptions (e.g. Microsoft Edge InPrivate mode is excluded, but Google Chrome isn’t).

Every user interaction, e.g. minimizing a window. There is an API for user activity, and third party apps can plug in to enrich data and also view store data.

It also stores all websites you visit, even if third party.

**Q. If I delete an email/WhatsApp/Signal/Teams message, is it deleted from Recall?**

A. No, it stays in the database indefinitely.

**Q. Are auto deleting messages in messaging apps removed from Recall?**

A. No, they’re scraped by Recall and available.

**Q. But if a hacker gains access to run code on your PC, it’s already game over!**

A. If you run something like an info stealer, at present they will automatically scrape things like credential stores. At scale, hackers scrape rather than touch every victim (because there are so many) and resell them in online marketplaces.

Recall enables threat actors to automate scraping everything you’ve ever looked at within seconds.

During testing this with an off the shelf infostealer, I used Microsoft Defender for Endpoint — which detected the off the shelve infostealer — but by the time the automated remediation kicked in (which took over ten minutes) my Recall data was already long gone.

**Q. Does this enable mass data breaches of websites?**

A. Yes. The next time you see a major data breach where customer data is clearly visible in the breach, you’re going to presume company who processes the data are at fault, right?

But if people have used a Windows device with Recall to access the service/app/whatever, hackers can see everything and assemble data dumps without the company who runs the service even being aware. The data is already consistently structured in the Recall database for attackers.

So prepare for AI powered super breaches. Currently credential marketplaces exist where you can buy stolen passwords — soon, you will be able to buy stolen customer data from insurance companies etc as the entire code to do this has been preinstalled and enabled on Windows by Microsoft.

**Q. Did Microsoft mislead the BBC about the security of Copilot?**

A. Yes.

**Q. Have Microsoft mislead customers about the security of Copilot?**

A. Yes. For example, they describe it as an optional experience — but it is enabled by default and people can optionally disable it. That’s wordsmithing.

Microsoft’s CEO referred to “screenshots” in an interview about the product, but the product itself only refers to “snapshots” — a snapshot is actually a screenshot. It’s again wordsmithing for whatever reason. Microsoft just need to be super clear about what this is, so customers can make an informed choice.

**Q. Recall only applies to 1 hardware device!**

A. That isn’t true. There are currently 10 Copilot+ devices available to order right now from every major manufacturer:

[https://www.microsoft.com/en-gb/windows/copilot-plus-pcs#shop](https://www.microsoft.com/en-gb/windows/copilot-plus-pcs#shop)

Additionally, Microsoft’s website say they are working on support for AMD and Intel chipsets. Recall is coming to Windows 11.

**Q. How do I disable Recall?**

A. In initial device setup for compatible Copilot+ devices out of the box, you have to click through options to disable Recall.

In enterprise, you have to turn off Recall as it is enabled by default:

![](https://miro.medium.com/v2/resize:fit:1050/1*zvmmJbRCaA-AHTnZ8AtjWQ.png)

[](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-windowsai?source=post_page-----da3e12e9465e--------------------------------#disableaidataanalysis)

## WindowsAI Policy CSP - Windows Client Management

### Learn more about the WindowsAI Area in Policy CSP

learn.microsoft.com

The Group Policy object for this has apparently been renamed (the MS documentation is incorrect):

**Q. What are the privacy implications? Isn’t this against GDPR?**

A. I am not a privacy person or a legal person.

I will say that privacy people I’ve talked to are extremely worried about the impacts on households in domestic abuse situations and such.

Obviously, from a corporate point of view organisations should absolutely consider the risk of processing customer data like this — Microsoft won’t be held responsible as the data processor, as it is done at the edge on your devices —  **you** are responsible here.

**Q. Are Microsoft a big, evil company?**

A. No, that’s insanely reductive. They’re super smart people, and sometimes super smart people make mistakes. What matters is what they do with knowledge of mistakes.

**Q. Aren’t you the former employee who hates Microsoft?**

A. No. I just wrote a blog this month praising them:

[](https://doublepulsar.com/breaking-down-microsofts-pivot-to-placing-cybersecurity-as-a-top-priority-734467a8db01?source=post_page-----da3e12e9465e--------------------------------)

## Breaking down Microsoft’s pivot to placing cybersecurity as a top priority

### My thoughts on Microsoft’s last chance saloon moment on security

doublepulsar.com

**Q. Is this really as harmful as you think?**

A. Go to your parents house, your grandparents house etc and look at their Windows PC, look at the installed software in the past year, and try to use the device. Run some antivirus scans. There’s no way this implementation doesn’t end in tears — there’s a reason there’s a trillion dollar security industry, and that most problems revolve around malware and endpoints.

**Q. What should Microsoft do?**

A. In my opinion — they should recall Recall and rework it to be the feature it deserves to be, delivered at a later date. They also need to review the internal decision making that led to this situation, as this kind of thing should not happen.
