import os
import anthropic
from github import Github

CODING_STANDARDS = """
# Trio C++ Coding Standards

## Naming
- enum_name: PascalCase, kısaltmalar kelime gibi (Io, Http) — örn: InputIoEventTriggerType
- enum_value: SCREAMING_SNAKE_CASE — örn: INPUT_IO_1
- function: snake_case — örn: get_frame_count()
- class/struct: PascalCase
- namespace: snake_case
- member_variable: snake_case, sonu _ ile — örn: frame_count_
- local_variable: snake_case

## Spacing
- Parantezin solunda 1 boşluk: if (), while (), for (), switch (), func ()

## Braces
- Allman style zorunlu (if, while, for, fonksiyon blokları)
- switch-case Allman style'dan muaftır

## Flow Control
- if, while, for, switch öncesinde 1 boş satır olmalı
- return öncesinde 1 boş satır olmalı
- if koşulu içinde değişken tanımlanmamalı
- Fonksiyon başına tek return kullanılmalı
- Döngüler içinde break, goto, continue kullanılmamalı
- switch-case içindeki break bu kuraldan muaftır
- switch bloklarında default case kullanılmamalı (-Wswitch aktif kalsın)

## Variables
- Tüm değişkenler tanımlanırken default değer almalı
"""

def review_file (file_path: str, content: str) -> str:
    client = anthropic.Anthropic (api_key=os.environ["ANTHROPIC_API_KEY"])

    prompt = f"""Aşağıdaki Trio C++ Coding Standards kurallarına göre verilen kodu denetle.
Sadece ihlalleri raporla. İhlal yoksa "✅ İhlal bulunamadı." yaz.
Her ihlal için şu formatı kullan:
- ❌ [KURAL_ADI] Satır X: açıklama

## Kurallar
{CODING_STANDARDS}

## Dosya: {file_path}
```cpp
{content}
```
"""

    message = client.messages.create (
        model="claude-sonnet-4-6",
        max_tokens=2048,
        messages=[{"role": "user", "content": prompt}]
    )

    return message.content[0].text


def main ():
    from github import Auth
    gh = Github (auth=Auth.Token (os.environ["GITHUB_TOKEN"]))
    repo = gh.get_repo (os.environ["REPO_NAME"])
    pr = repo.get_pull (int (os.environ["PR_NUMBER"]))

    changed_files_path = "changed_files.txt"

    with open (changed_files_path) as f:
        changed_files = [line.strip () for line in f if line.strip ()]

    if not changed_files:
        print ("Değişen C++ dosyası bulunamadı.")
        return

    full_report = "## 🤖 Claude Code Review — Trio Coding Standards\n\n"

    for file_path in changed_files:
        if not os.path.exists (file_path):
            continue

        with open (file_path, encoding="utf-8", errors="ignore") as f:
            content = f.read ()

        print (f"Reviewing: {file_path}")
        review = review_file (file_path, content)
        full_report += f"### 📄 `{file_path}`\n{review}\n\n---\n\n"

    pr.create_issue_comment (full_report)
    print ("Review PR'a yorum olarak eklendi.")


if __name__ == "__main__":
    main ()
