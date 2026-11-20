package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
)

func main() {
	os.Exit(runCLI(os.Args[1:]))
}

func runCLI(args []string) int {
	fs := flag.NewFlagSet("datacompiler", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)

	var opts Options
	fs.BoolVar(&opts.Validate, "validate", false, "Run 4-gate schema, IP hygiene, referential integrity, and math validation")
	fs.BoolVar(&opts.Export, "export", false, "Export Go bundle (SHA-256) and UE5 UDataTable JSON/CSV outputs")
	fs.StringVar(&opts.SrcDir, "src", "", "Source gamedata directory containing core/ and packs/")
	fs.StringVar(&opts.SchemasDir, "schemas", "", "Directory containing Draft 2020-12 *.schema.json files")
	fs.StringVar(&opts.IPPack, "ip-pack", "Pack_Original_Commercial", "Presentation pack name under <src>/packs/")
	fs.StringVar(&opts.OutDir, "out", "", "Build output directory for backend/ and unreal/ artifacts")
	fs.StringVar(&opts.Locale, "locale", "vi", "Primary locale for DT_PresentationPack export (vi or en)")

	if err := fs.Parse(args); err != nil {
		return 2
	}

	if !opts.Validate && !opts.Export {
		fmt.Fprintln(os.Stderr, "error: specify --validate and/or --export")
		fs.Usage()
		return 1
	}

	if opts.SrcDir == "" || opts.SchemasDir == "" || (opts.Export && opts.OutDir == "") {
		gdRoot := detectGamedataRoot()
		if gdRoot != "" {
			if opts.SrcDir == "" {
				opts.SrcDir = filepath.Join(gdRoot, "sources")
			}
			if opts.SchemasDir == "" {
				opts.SchemasDir = filepath.Join(gdRoot, "schemas")
			}
			if opts.OutDir == "" {
				opts.OutDir = filepath.Join(gdRoot, "build")
			}
		}
	}

	res, err := Run(opts)
	if err != nil {
		fmt.Fprintln(os.Stderr, err.Error())
		return 1
	}

	mode := "validated"
	if opts.Export {
		mode = "validated+exported"
	}
	fmt.Printf("datacompiler: %s ip_pack=%s gamedata_hash=%s elapsed=%s\n",
		mode, res.IPPack, res.GameDataHash, res.Elapsed.Round(1e6))
	return 0
}

func detectGamedataRoot() string {
	cwd, err := os.Getwd()
	if err != nil {
		return ""
	}
	candidates := []string{
		filepath.Join(cwd, "gamedata"),
		filepath.Join(cwd, "..", "gamedata"),
		filepath.Join(cwd, "..", "..", "gamedata"),
		filepath.Join(cwd, "..", "..", "..", "gamedata"),
	}
	for _, cand := range candidates {
		if info, err := os.Stat(cand); err == nil && info.IsDir() {
			abs, err := filepath.Abs(cand)
			if err == nil {
				return abs
			}
			return cand
		}
	}
	return ""
}
