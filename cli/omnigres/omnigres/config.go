package omnigres

import (
	"github.com/kirsle/configdir"
	"gopkg.in/yaml.v3"
	"os"
	"path"
)

type Config struct {
	Revisions []string
}

func ConfigFilename() string {
	localCache := configdir.LocalCache("omnigres")
	return path.Join(localCache, "cache.yml")
}

func LoadConfig() (cfg Config, err error) {
	configBytes, err := os.ReadFile(ConfigFilename())
	if os.IsNotExist(err) {
		err = nil
		return
	}
	if err != nil {
		return
	}
	err = yaml.Unmarshal(configBytes, &cfg)
	return
}

func (cfg *Config) Save() (err error) {
	var out []byte
	out, err = yaml.Marshal(cfg)
	os.WriteFile(ConfigFilename(), out, 0o644)
	return
}
