from flask import Flask, request, render_template, redirect, url_for
import os
from tools.extract import extract_chunks

app = Flask(__name__)
UPLOAD_FOLDER = "uploads"
EXTRACT_FOLDER = "output_chunks"
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

@app.route("/", methods=["GET", "POST"])
def index():
    if request.method == "POST":
        wad_file = request.files.get("wad_file")
        if wad_file:
            path = os.path.join(UPLOAD_FOLDER, wad_file.filename)
            wad_file.save(path)
            extract_chunks(wad_path=path)  # call your logic with override
            return redirect(url_for("result", wad_name=wad_file.filename))
    return render_template("index.html")

@app.route("/result/<wad_name>")
def result(wad_name):
    return f"✅ Extracted from: {wad_name}! Check the output_chunks folder."

if __name__ == "__main__":
    app.run(debug=True)
