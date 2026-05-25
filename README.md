# Wii Arena

```python
with DolphinUniverse(author=GrandPrixScenarioAuthor(...)).session() as environment:
    observation, context = environment.reset()
    terminated, truncated = Terminated(False), Truncated(False)

    while not (terminated or truncated):
        action = agent.act(observation)
        observation, terminated, truncated, context = environment.step(action)

        if terminated or truncated:
            break
```
