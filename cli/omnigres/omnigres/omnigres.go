package omnigres

import "strings"

func InferRevisionFromTarballName(filename string) string {
	return strings.Split(strings.Split(filename, ".")[0], "-")[2]
}
